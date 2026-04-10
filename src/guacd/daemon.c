/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifdef _WIN32
#include "config.win32.h"
#else
#include "config.h"
#endif

#include "conf.h"
#include "conf-args.h"
#include "conf-file.h"
#include "connection.h"
#include "log.h"
#include "proc-map.h"

/* Public embedding API - exported from guacd.dll */
#define GUACD_EXPORTS 1
#include "guacd-api.h"

#include <guacamole/mem.h>

#ifdef ENABLE_SSL
#include <openssl/ssl.h>
#endif

#ifdef _WIN32
#include "win32-compat.h"
#else
#include <libgen.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define GUACD_DEV_NULL "NUL"
#define GUACD_ROOT     "C:\\"
#else
#define GUACD_DEV_NULL "/dev/null"
#define GUACD_ROOT     "/"
#endif

#ifndef _WIN32
/**
 * Redirects the given file descriptor to /dev/null. The given flags must match
 * the read/write flags of the file descriptor given (if the given file
 * descriptor was opened write-only, flags here must be O_WRONLY, etc.).
 *
 * @param fd
 *     The file descriptor to redirect to /dev/null.
 *
 * @param flags
 *     The flags to use when opening /dev/null as the target for redirection.
 *     These flags must match the flags of the file descriptor given.
 *
 * @return
 *     Zero on success, non-zero if redirecting the file descriptor fails.
 */
static int redirect_fd(int fd, int flags) {

    /* Attempt to open bit bucket */
    int new_fd = open(GUACD_DEV_NULL, flags);
    if (new_fd < 0)
        return 1;

    /* If descriptor is different, redirect old to new and close new */
    if (new_fd != fd) {
        dup2(new_fd, fd);
        close(new_fd);
    }

    return 0;

}
#endif /* !_WIN32 */

#ifndef _WIN32
/**
 * Turns the current process into a daemon through a series of fork() calls.
 * The standard I/O file descriptors for STDIN, STDOUT, and STDERR will be
 * redirected to /dev/null, and the working directory is changed to root.
 * Execution within the caller of this function will terminate before this
 * function returns, while execution within the daemonized child process will
 * continue.
 *
 * @return
 *    Zero if the daemonization process succeeded and we are now in the
 *    daemonized child process, or non-zero if daemonization failed and we are
 *    still the original caller. This function does not return for the original
 *    caller if daemonization succeeds.
 */
static int daemonize(void) {

    pid_t pid;

    /* Fork once to ensure we aren't the process group leader */
    pid = fork();
    if (pid < 0) {
        guacd_log(GUAC_LOG_ERROR, "Could not fork() parent: %s", strerror(errno));
        return 1;
    }

    /* Exit if we are the parent */
    if (pid > 0) {
        guacd_log(GUAC_LOG_DEBUG, "Exiting and passing control to PID %i", pid);
        _exit(0);
    }

    /* Start a new session (if not already group leader) */
    setsid();

    /* Fork again so the session group leader exits */
    pid = fork();
    if (pid < 0) {
        guacd_log(GUAC_LOG_ERROR, "Could not fork() group leader: %s", strerror(errno));
        return 1;
    }

    /* Exit if we are the parent */
    if (pid > 0) {
        guacd_log(GUAC_LOG_DEBUG, "Exiting and passing control to PID %i", pid);
        _exit(0);
    }

    /* Change to root directory */
    if (chdir(GUACD_ROOT) < 0) {
        guacd_log(GUAC_LOG_ERROR,
                "Unable to change working directory to "
                GUACD_ROOT);
        return 1;
    }

    /* Reopen the 3 stdxxx to /dev/null */

    if (redirect_fd(STDIN_FILENO, O_RDONLY)
    || redirect_fd(STDOUT_FILENO, O_WRONLY)
    || redirect_fd(STDERR_FILENO, O_WRONLY)) {

        guacd_log(GUAC_LOG_ERROR,
                "Unable to redirect standard file descriptors to "
                GUACD_DEV_NULL);
        return 1;
    }

    /* Success */
    return 0;

}
#endif /* !_WIN32 */

#ifdef ENABLE_SSL
#ifdef OPENSSL_REQUIRES_THREADING_CALLBACKS
/**
 * Array of mutexes, used by OpenSSL.
 */
static pthread_mutex_t* guacd_openssl_locks = NULL;

/**
 * Called by OpenSSL when locking or unlocking the Nth mutex.
 *
 * @param mode
 *     A bitmask denoting the action to be taken on the Nth lock, such as
 *     CRYPTO_LOCK or CRYPTO_UNLOCK.
 *
 * @param n
 *     The index of the lock to lock or unlock.
 *
 * @param file
 *     The filename of the function setting the lock, for debugging purposes.
 *
 * @param line
 *     The line number of the function setting the lock, for debugging
 *     purposes.
 */
static void guacd_openssl_locking_callback(int mode, int n,
        const char* file, int line){

    /* Lock given mutex upon request */
    if (mode & CRYPTO_LOCK)
        pthread_mutex_lock(&(guacd_openssl_locks[n]));

    /* Unlock given mutex upon request */
    else if (mode & CRYPTO_UNLOCK)
        pthread_mutex_unlock(&(guacd_openssl_locks[n]));

}

/**
 * Called by OpenSSL when determining the current thread ID.
 *
 * @return
 *     An ID which uniquely identifies the current thread.
 */
static unsigned long guacd_openssl_id_callback(void) {
    return (unsigned long) pthread_self();
}

/**
 * Creates the given number of mutexes, such that OpenSSL will have at least
 * this number of mutexes at its disposal.
 *
 * @param count
 *     The number of mutexes (locks) to create.
 */
static void guacd_openssl_init_locks(int count) {

    int i;

    /* Allocate required number of locks */
    guacd_openssl_locks = guac_mem_alloc(sizeof(pthread_mutex_t), count);

    /* Initialize each lock */
    for (i=0; i < count; i++)
        pthread_mutex_init(&(guacd_openssl_locks[i]), NULL);

}

/**
 * Frees the given number of mutexes.
 *
 * @param count
 *     The number of mutexes (locks) to free.
 */
static void guacd_openssl_free_locks(int count) {

    int i;

    /* SSL lock array was not initialized */
    if (guacd_openssl_locks == NULL)
        return;

    /* Free all locks */
    for (i=0; i < count; i++)
        pthread_mutex_destroy(&(guacd_openssl_locks[i]));

    /* Free lock array */
    guac_mem_free(guacd_openssl_locks);

}
#endif
#endif

/**
 * A flag that, if non-zero, indicates that the daemon should immediately stop
 * accepting new connections.
 */
int stop_everything = 0;

#ifdef _WIN32
/**
 * A Windows console control handler that will set a flag telling the daemon
 * to immediately stop accepting new connections.
 *
 * @param dwCtrlType
 *     The type of control signal received.
 *
 * @return
 *     TRUE if the signal was handled, FALSE otherwise.
 */
static BOOL WINAPI win32_ctrl_handler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT
            || dwCtrlType == CTRL_BREAK_EVENT
            || dwCtrlType == CTRL_CLOSE_EVENT) {
        stop_everything = 1;
        return TRUE;
    }
    return FALSE;
}
#else
/**
 * A signal handler that will set a flag telling the daemon to immediately stop
 * accepting new connections. Note that the signal itself will cause any pending
 * accept() calls to be interrupted, causing the daemon to unlock and begin
 * cleaning up.
 *
 * @param signal
 *     The signal that was received. Unused in this function since only
 *     signals that should result in stopping the daemon should invoke this.
 */
static void signal_stop_handler(int signal) {

    /* Instruct the daemon to stop accepting new connections */
    stop_everything = 1;

}
#endif /* _WIN32 */

/**
 * A callback for guacd_proc_map_foreach which will stop every process in the
 * map.
 *
 * @param proc
 *     The guacd process to stop.
 *
 * @param data
 *     Unused.
 */
static void stop_process_callback(guacd_proc* proc, void* data) {

    guacd_log(GUAC_LOG_DEBUG,
            "Killing connection %s (%i)\n",
            proc->client->connection_id, (int) proc->pid);

    guacd_proc_stop(proc);

}

/* Public version string accessor for DLL consumers. */
GUACD_API const char* guacd_version(void) {
    return VERSION;
}

/* Public shutdown trigger for DLL consumers.
 * The "instance" pointer is currently unused because guacd's state is
 * process-global (stop_everything is a single flag). The parameter is
 * kept so the API can grow to support multiple instances in the future
 * without breaking ABI. Safe to pass NULL. */
GUACD_API void guacd_stop(guacd_instance* instance) {
    (void)instance;
    stop_everything = 1;
}

/*
 * Command-line compatible entry point. Exported from guacd.dll so an
 * embedding application can invoke the daemon with the same arguments
 * it would pass to the standalone guacd.exe.
 */
GUACD_API int guacd_main(int argc, char* argv[]) {

#ifdef _WIN32
    /* Initialize Winsock before any socket operations */
    guacd_winsock_init();
#endif

    /* Server */
    int socket_fd;
    struct addrinfo* addresses;
    struct addrinfo* current_address;
    char bound_address[1024];
    char bound_port[64];
    int opt_on = 1;

    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };

    /* Client */
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    int connected_socket_fd;

#ifdef ENABLE_SSL
    SSL_CTX* ssl_context = NULL;
#endif

    guacd_proc_map* map = guacd_proc_map_alloc();

    /* General */
    int retval;

#ifdef HAVE_DECL_PTHREAD_SETATTR_DEFAULT_NP
    /* Set default stack size */
    pthread_attr_t default_pthread_attr;
    pthread_attr_init(&default_pthread_attr);
    pthread_attr_setstacksize(&default_pthread_attr, GUACD_THREAD_STACK_SIZE);
    pthread_setattr_default_np(&default_pthread_attr);
#endif // HAVE_DECL_PTHREAD_SETATTR_DEFAULT_NP

    /* Load configuration */
    guacd_config* config = guacd_conf_load();
    if (config == NULL || guacd_conf_parse_args(config, argc, argv))
       exit(EXIT_FAILURE);

    /* If requested, simply print version and exit, without initializing the
     * logging system, etc. */
    if (config->print_version) {
        printf("Guacamole proxy daemon (guacd) version " VERSION "\n");
        exit(EXIT_SUCCESS);
    }

    /* Init logging as early as possible */
    guacd_log_level = config->max_log_level;
#ifndef _WIN32
    openlog(GUACD_LOG_NAME, LOG_PID, LOG_DAEMON);
#endif

#ifdef _WIN32
    /* Windows does not support daemonization; always run in foreground */
    config->foreground = 1;
#endif

    /* Log start */
    guacd_log(GUAC_LOG_INFO, "Guacamole proxy daemon (guacd) version " VERSION " started");

    /* Get addresses for binding */
    if ((retval = getaddrinfo(config->bind_host, config->bind_port,
                    &hints, &addresses))) {

        guacd_log(GUAC_LOG_ERROR, "Error parsing given address or port: %s",
                gai_strerror(retval));
        exit(EXIT_FAILURE);

    }

    /* Attempt binding of each address until success */
    current_address = addresses;
    while (current_address != NULL) {

        int retval;

        /* Resolve hostname */
        if ((retval = getnameinfo(current_address->ai_addr,
                current_address->ai_addrlen,
                bound_address, sizeof(bound_address),
                bound_port, sizeof(bound_port),
                NI_NUMERICHOST | NI_NUMERICSERV)))
            guacd_log(GUAC_LOG_ERROR, "Unable to resolve host: %s",
                    gai_strerror(retval));

        /* Get socket */
        socket_fd = socket(current_address->ai_family, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            guacd_log(GUAC_LOG_ERROR, "Error opening socket: %s", strerror(errno));

            /* Unable to get a socket for the resolved address family, try next */
            current_address = current_address->ai_next;
            continue;
        }

        /* Allow socket reuse */
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                    (void*) &opt_on, sizeof(opt_on))) {
            guacd_log(GUAC_LOG_WARNING, "Unable to set socket options for reuse: %s",
                    strerror(errno));
        }

        /* Attempt to bind socket to address */
        if (bind(socket_fd,
                    current_address->ai_addr,
                    current_address->ai_addrlen) == 0) {

            guacd_log(GUAC_LOG_DEBUG, "Successfully bound "
                    "%s socket to host %s, port %s",
                    (current_address->ai_family == AF_INET) ? "AF_INET" : "AF_INET6",
                    bound_address, bound_port);

            /* Done if successful bind */
            break;
        }

        /* Otherwise log information regarding bind failure */
        guacd_close_socket(socket_fd);
        socket_fd = -1;
        guacd_log(GUAC_LOG_DEBUG, "Unable to bind %s socket to "
                "host %s, port %s: %s",
                (current_address->ai_family == AF_INET) ? "AF_INET" : "AF_INET6",
                bound_address, bound_port, strerror(errno));

        /* Try next address */
        current_address = current_address->ai_next;
    }

    /* If unable to bind to anything, fail */
    if (current_address == NULL) {
        guacd_log(GUAC_LOG_ERROR, "Unable to bind socket to any addresses.");
        exit(EXIT_FAILURE);
    }

#ifdef ENABLE_SSL
    /* Init SSL if enabled */
    if (config->key_file != NULL || config->cert_file != NULL) {

        guacd_log(GUAC_LOG_INFO, "Communication will require SSL/TLS.");

#ifdef OPENSSL_REQUIRES_THREADING_CALLBACKS
        /* Init threadsafety in OpenSSL */
        guacd_openssl_init_locks(CRYPTO_num_locks());
        CRYPTO_set_id_callback(guacd_openssl_id_callback);
        CRYPTO_set_locking_callback(guacd_openssl_locking_callback);
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L
        /* Init OpenSSL for OpenSSL Versions < 1.1.0 */
        SSL_library_init();
        SSL_load_error_strings();
        ssl_context = SSL_CTX_new(SSLv23_server_method());
#else
        /* Set up OpenSSL for OpenSSL Versions >= 1.1.0 */
        ssl_context = SSL_CTX_new(TLS_server_method());
#endif

        /* Load key */
        if (config->key_file != NULL) {
            guacd_log(GUAC_LOG_INFO, "Using PEM keyfile %s", config->key_file);
            if (!SSL_CTX_use_PrivateKey_file(ssl_context, config->key_file, SSL_FILETYPE_PEM)) {
                guacd_log(GUAC_LOG_ERROR, "Unable to load keyfile.");
                exit(EXIT_FAILURE);
            }
        }
        else
            guacd_log(GUAC_LOG_WARNING, "No PEM keyfile given - SSL/TLS may not work.");

        /* Load cert file if specified */
        if (config->cert_file != NULL) {
            guacd_log(GUAC_LOG_INFO, "Using certificate file %s", config->cert_file);
            if (!SSL_CTX_use_certificate_chain_file(ssl_context, config->cert_file)) {
                guacd_log(GUAC_LOG_ERROR, "Unable to load certificate.");
                exit(EXIT_FAILURE);
            }
        }
        else
            guacd_log(GUAC_LOG_WARNING, "No certificate file given - SSL/TLS may not work.");

    }
#endif

#ifndef _WIN32
    /* Daemonize if requested */
    if (!config->foreground) {

        /* Attempt to daemonize process */
        if (daemonize()) {
            guacd_log(GUAC_LOG_ERROR, "Could not become a daemon.");
            exit(EXIT_FAILURE);
        }

    }
#endif /* !_WIN32 */

    /* Write PID file if requested */
    if (config->pidfile != NULL) {

        /* Attempt to open pidfile and write PID */
        FILE* pidf = fopen(config->pidfile, "w");
        if (pidf) {
            fprintf(pidf, "%d\n", guacd_getpid());
            fclose(pidf);
        }

        /* Fail if could not write PID file*/
        else {
            guacd_log(GUAC_LOG_ERROR, "Could not write PID file: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

    }

#ifdef _WIN32
    /* Set up Windows console control handler for graceful shutdown */
    SetConsoleCtrlHandler(win32_ctrl_handler, TRUE);
#else
    /* Ignore SIGPIPE */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        guacd_log(GUAC_LOG_INFO, "Could not set handler for SIGPIPE to ignore. "
                "SIGPIPE may cause termination of the daemon.");
    }

    /* Ignore SIGCHLD (force automatic removal of children) */
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        guacd_log(GUAC_LOG_INFO, "Could not set handler for SIGCHLD to ignore. "
                "Child processes may pile up in the process table.");
    }

    /* Clean up and exit if SIGINT or SIGTERM signals are caught; don't set
       SA_RESTART as we rely on accept() to return EINTR.*/
    struct sigaction signal_stop_action = { .sa_handler = signal_stop_handler };
    sigaction(SIGINT, &signal_stop_action, NULL);
    sigaction(SIGTERM, &signal_stop_action, NULL);
#endif /* _WIN32 */

#ifdef HAVE_PRCTL
    /* Adopt any orphan processes from clients so we can properly terminate and reap */
    prctl(PR_SET_CHILD_SUBREAPER, 1);
#else
    guacd_log(GUAC_LOG_INFO, "prctl not available on this platform. "
            "Orphan child processes may pile up in the process table.");
#endif

    /* Log listening status */
    guacd_log(GUAC_LOG_INFO, "Listening on host %s, port %s", bound_address, bound_port);

    /* Free addresses */
    freeaddrinfo(addresses);

    /* Listen for connections */
    if (listen(socket_fd, 5) < 0) {
        guacd_log(GUAC_LOG_ERROR, "Could not listen on socket: %s", strerror(errno));
        return 3;
    }

    /* Daemon loop */
    while (!stop_everything) {

        pthread_t child_thread;

        /* Accept connection */
#ifdef _WIN32
        /* On Windows (with pthreads4w), a plain blocking accept() in the
         * main thread sometimes hangs even when incoming connections are
         * pending. Use select() as a wait primitive that properly yields
         * to other threads, then call accept() non-blockingly. */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET((SOCKET)socket_fd, &readfds);
        struct timeval tv = {1, 0};  /* 1 second timeout so we can check stop_everything */
        int sel = select(0, &readfds, NULL, NULL, &tv);
        if (sel <= 0) continue;
#endif
        client_addr_len = sizeof(client_addr);
        connected_socket_fd = accept(socket_fd,
                (struct sockaddr*) &client_addr, &client_addr_len);

        if (connected_socket_fd < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR)
#else
            if (errno == EINTR)
#endif
                guacd_log(GUAC_LOG_DEBUG, "Accepting of further client connection(s) interrupted by signal.");
            else
                guacd_log(GUAC_LOG_ERROR, "Could not accept client connection: %s", strerror(errno));
            continue;
        }

        /* Set TCP_NODELAY to avoid any latency that would otherwise be added by the OS'
         * networking stack and Nagle's algorithm */
        const int SO_TRUE = 1;
        setsockopt(connected_socket_fd, IPPROTO_TCP, TCP_NODELAY,
                (const void*) &SO_TRUE, sizeof(SO_TRUE));

        /* Create parameters for connection thread */
        guacd_connection_thread_params* params = guac_mem_alloc(sizeof(guacd_connection_thread_params));
        if (params == NULL) {
            guacd_log(GUAC_LOG_ERROR, "Could not create connection thread: %s", strerror(errno));
            continue;
        }

        params->map = map;
        params->connected_socket_fd = connected_socket_fd;

#ifdef ENABLE_SSL
        params->ssl_context = ssl_context;
#endif

        /* Spawn thread to handle connection */
        pthread_create(&child_thread, NULL, guacd_connection_thread, params);
        pthread_detach(child_thread);

    }

    /* Stop all connections */
    if (map != NULL) {

        guacd_proc_map_foreach(map, stop_process_callback, NULL);

        /*
         * FIXME: Clean up the proc map. This is not as straightforward as it
         * might seem, since the detached connection threads will attempt to
         * remove the connection processes from the map when they complete,
         * which will also happen upon shutdown. So there's a good chance that
         * this map cleanup will happen at the same time as the thread cleanup.
         * The map _does_ have locking mechanisms in place for ensuring thread
         * safety, but cleaning up the map also requires destroying those locks,
         * making them unusable for this case. One potential fix could be to
         * join every one of the connection threads instead of detaching them,
         * but that does complicate the cleanup of thread resources.
         */

    }

    /* Close socket */
    if (guacd_close_socket(socket_fd) < 0) {
        guacd_log(GUAC_LOG_ERROR, "Could not close socket: %s", strerror(errno));
        return 3;
    }

#ifdef ENABLE_SSL
    if (ssl_context != NULL) {
#ifdef OPENSSL_REQUIRES_THREADING_CALLBACKS
        guacd_openssl_free_locks(CRYPTO_num_locks());
#endif
        SSL_CTX_free(ssl_context);
    }
#endif

#ifdef _WIN32
    guacd_winsock_cleanup();
#endif

    return 0;

}

/* ======================================================================
 * Programmatic embedding API
 *
 * The opaque guacd_instance currently only serves as a holder for the
 * background thread handle and a saved copy of argv built from the
 * options struct. The listener state itself is still process-global
 * (stop_everything, proc_map, etc.) - only one instance per process
 * is supported for now. The API is designed so this can change later
 * without breaking callers.
 * ====================================================================== */

struct guacd_instance {
    /* argv buffer we build from guacd_options and feed to guacd_main() */
    int argc;
    char* argv_storage[16];      /* max 15 args + NULL terminator */
    char  argv_buffer[2048];     /* backing storage for argument strings */

    /* Background thread when started via guacd_run_async() */
    pthread_t thread;
    int thread_running;
    int thread_rc;
};

/* Append a NUL-terminated string to argv_buffer and register it as
 * the next argv[] slot. Returns 0 on success, -1 on overflow. */
static int guacd_instance_push_arg(guacd_instance* inst, const char* s) {
    if (inst->argc >= 15) return -1;
    size_t used = 0;
    for (int i = 0; i < inst->argc; i++) {
        used += strlen(inst->argv_storage[i]) + 1;
    }
    size_t need = strlen(s) + 1;
    if (used + need > sizeof(inst->argv_buffer)) return -1;
    char* dst = inst->argv_buffer + used;
    memcpy(dst, s, need);
    inst->argv_storage[inst->argc++] = dst;
    inst->argv_storage[inst->argc] = NULL;
    return 0;
}

GUACD_API guacd_instance* guacd_create(const guacd_options* opts) {
    if (opts == NULL) return NULL;

    guacd_instance* inst = (guacd_instance*) calloc(1, sizeof(*inst));
    if (inst == NULL) return NULL;

    /* argv[0] is the program name */
    if (guacd_instance_push_arg(inst, "guacd") != 0) { free(inst); return NULL; }

    char scratch[64];

    if (opts->bind_host && opts->bind_host[0]) {
        if (guacd_instance_push_arg(inst, "-b") != 0 ||
            guacd_instance_push_arg(inst, opts->bind_host) != 0)
            { free(inst); return NULL; }
    }

    if (opts->bind_port > 0) {
        snprintf(scratch, sizeof(scratch), "%d", opts->bind_port);
        if (guacd_instance_push_arg(inst, "-l") != 0 ||
            guacd_instance_push_arg(inst, scratch) != 0)
            { free(inst); return NULL; }
    }

    if (opts->log_level >= 0 && opts->log_level <= 4) {
        static const char* levels[] = { "error", "warning", "info", "debug", "trace" };
        if (guacd_instance_push_arg(inst, "-L") != 0 ||
            guacd_instance_push_arg(inst, levels[opts->log_level]) != 0)
            { free(inst); return NULL; }
    }

    if (opts->pidfile && opts->pidfile[0]) {
        if (guacd_instance_push_arg(inst, "-p") != 0 ||
            guacd_instance_push_arg(inst, opts->pidfile) != 0)
            { free(inst); return NULL; }
    }

#ifdef ENABLE_SSL
    if (opts->cert_file && opts->cert_file[0]) {
        if (guacd_instance_push_arg(inst, "-C") != 0 ||
            guacd_instance_push_arg(inst, opts->cert_file) != 0)
            { free(inst); return NULL; }
    }
    if (opts->key_file && opts->key_file[0]) {
        if (guacd_instance_push_arg(inst, "-K") != 0 ||
            guacd_instance_push_arg(inst, opts->key_file) != 0)
            { free(inst); return NULL; }
    }
#endif

    /* Always run in foreground - required on Windows anyway */
    (void) guacd_instance_push_arg(inst, "-f");

    return inst;
}

GUACD_API int guacd_run(guacd_instance* instance) {
    if (instance == NULL) return -1;
    stop_everything = 0;
    return guacd_main(instance->argc, instance->argv_storage);
}

static void* guacd_async_thread(void* data) {
    guacd_instance* inst = (guacd_instance*) data;
    inst->thread_rc = guacd_main(inst->argc, inst->argv_storage);
    inst->thread_running = 0;
    return NULL;
}

GUACD_API int guacd_run_async(guacd_instance* instance) {
    if (instance == NULL) return -1;
    if (instance->thread_running) return 0;  /* already started */
    stop_everything = 0;
    instance->thread_running = 1;
    if (pthread_create(&instance->thread, NULL,
                       guacd_async_thread, instance) != 0) {
        instance->thread_running = 0;
        return -1;
    }
    return 0;
}

GUACD_API void guacd_free(guacd_instance* instance) {
    if (instance == NULL) return;

    if (instance->thread_running) {
        stop_everything = 1;
        /* Wait up to ~5 seconds for the background thread to exit.
         * The accept loop has a 1-second select() timeout on Windows,
         * so this should complete within 1-2 seconds in practice. */
        for (int i = 0; i < 50 && instance->thread_running; i++) {
#ifdef _WIN32
            Sleep(100);
#else
            struct timespec ts = { 0, 100 * 1000 * 1000 };
            nanosleep(&ts, NULL);
#endif
        }
        if (instance->thread_running) {
            /* Thread still hasn't exited - detach and leak rather than
             * block forever. The process is likely shutting down. */
            pthread_detach(instance->thread);
        } else {
            pthread_join(instance->thread, NULL);
        }
    }

    free(instance);
}

/* ======================================================================
 * Standalone executable entry point
 * Only compiled when not building as a DLL.
 * ====================================================================== */
#ifndef GUACD_BUILD_DLL
int main(int argc, char* argv[]) {
    return guacd_main(argc, argv);
}
#endif

