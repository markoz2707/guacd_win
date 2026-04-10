#ifndef GUACD_WIN32_COMPAT_H
#define GUACD_WIN32_COMPAT_H

#ifdef _WIN32

/* Must be included before windows.h */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <process.h>

/* Link with ws2_32.lib */
#pragma comment(lib, "ws2_32.lib")

/* Map POSIX close() to closesocket() for sockets */
/* NOTE: callers must use guacd_close_socket() for sockets
   and _close() for file descriptors */
static inline int guacd_close_socket(int fd) {
    return closesocket((SOCKET)fd);
}

/* getpid */
#define guacd_getpid _getpid

/* SIGPIPE doesn't exist on Windows */
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGCHLD
#define SIGCHLD 17
#endif
#ifndef SIG_IGN
#define SIG_IGN ((void(*)(int))1)
#endif

/* Syslog replacements - just map to log levels */
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_INFO    6
#define LOG_DEBUG   7
#define LOG_PID     0x01
#define LOG_DAEMON  (3<<3)

static inline void openlog(const char* ident, int option, int facility) { (void)ident; (void)option; (void)facility; }
static inline void closelog(void) {}
static inline void syslog(int priority, const char* format, ...) { (void)priority; (void)format; }

/* EINTR retry macro - Windows doesn't use EINTR the same way */
#ifndef GUAC_RETRY_EINTR
#define GUAC_RETRY_EINTR(result, expr) do { result = (expr); } while(0)
#endif

/* Replace socketpair with TCP loopback pair */
static inline int win32_socketpair(int socks[2]) {
    SOCKET listener, client, server;
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) goto error;
    if (getsockname(listener, (struct sockaddr*)&addr, &addrlen) == SOCKET_ERROR) goto error;
    if (listen(listener, 1) == SOCKET_ERROR) goto error;

    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) goto error;

    if (connect(client, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(client);
        goto error;
    }

    server = accept(listener, NULL, NULL);
    if (server == INVALID_SOCKET) {
        closesocket(client);
        goto error;
    }

    closesocket(listener);
    socks[0] = (int)client;
    socks[1] = (int)server;
    return 0;

error:
    closesocket(listener);
    return -1;
}

/* pid_t doesn't exist on Windows */
typedef int pid_t;

/* SHUT_RDWR not defined on Windows */
#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

/* gettimeofday replacement for Windows */
#include <time.h>
struct timezone;
static inline int gettimeofday(struct timeval* tv, struct timezone* tz) {
    (void)tz;
    if (tv) {
        FILETIME ft;
        ULARGE_INTEGER li;
        GetSystemTimeAsFileTime(&ft);
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        /* Convert from 100-nanosecond intervals since 1601 to Unix epoch */
        li.QuadPart -= 116444736000000000ULL;
        tv->tv_sec = (long)(li.QuadPart / 10000000);
        tv->tv_usec = (long)((li.QuadPart % 10000000) / 10);
    }
    return 0;
}

/* struct timespec - already defined in UCRT time.h on modern MSVC */

/* Initialize Winsock - call this at program start */
static inline int guacd_winsock_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void guacd_winsock_cleanup(void) {
    WSACleanup();
}

#else /* !_WIN32 (Unix) */

#define guacd_close_socket close
#define guacd_getpid getpid

static inline int guacd_winsock_init(void) { return 0; }
static inline void guacd_winsock_cleanup(void) {}

#endif /* _WIN32 */

#endif /* GUACD_WIN32_COMPAT_H */
