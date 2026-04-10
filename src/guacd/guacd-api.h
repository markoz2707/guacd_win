/*
 * guacd public embedding API.
 *
 * When you link against guacd.dll (or its import library) you use these
 * functions to initialize, run, stop, and tear down a guacd instance from
 * within your own executable.
 *
 * Two usage patterns:
 *
 * 1. Drop-in command-line replacement:
 *        guacd_main(argc, argv);
 *    Accepts the same command-line flags as guacd.exe (-l, -b, -L, -C, -K,
 *    -p, -f, -v). Blocks until the listener stops or fatal error.
 *
 * 2. Programmatic embedding:
 *        guacd_instance* g = guacd_create(&opts);   // allocate + configure
 *        int rc = guacd_run(g);                      // blocks (listen/accept)
 *        // from another thread:
 *        guacd_stop(g);                              // ask for graceful shutdown
 *        guacd_free(g);                              // release resources
 *
 *    Or let guacd own the listener thread for you:
 *        guacd_instance* g = guacd_create(&opts);
 *        guacd_run_async(g);                         // returns immediately
 *        ...                                         // do other work
 *        guacd_stop(g);
 *        guacd_free(g);                              // waits for thread
 */

#ifndef GUACD_API_H
#define GUACD_API_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#   if defined(GUACD_BUILD_DLL) && defined(GUACD_EXPORTS)
#       define GUACD_API __declspec(dllexport)
#   elif defined(GUACD_USE_DLL)
#       define GUACD_API __declspec(dllimport)
#   else
#       define GUACD_API
#   endif
#else
#   define GUACD_API __attribute__((visibility("default")))
#endif

/**
 * Opaque handle for a running guacd instance.
 */
typedef struct guacd_instance guacd_instance;

/**
 * Options controlling how guacd_create() configures a new instance.
 * Zero-initialize this struct first (memset or {0}), then fill the fields
 * you care about. All string pointers are copied internally - the caller
 * does not need to keep them alive.
 */
typedef struct guacd_options {

    /** Listen address. NULL or empty => "0.0.0.0". */
    const char* bind_host;

    /** Listen port. 0 => 4822. */
    int bind_port;

    /**
     * Directory to search for protocol plugin DLLs (guac-client-*.dll).
     * NULL => directory of the current executable (via GetModuleFileName).
     * Only meaningful if you've overridden GUAC_LIB_DIR at compile time;
     * otherwise the plugin path is resolved the same way as the standalone
     * exe resolves it.
     */
    const char* plugin_dir;

    /**
     * Maximum log level to emit on stderr:
     *   0 = error only
     *   1 = warning
     *   2 = info    (default)
     *   3 = debug
     *   4 = trace
     */
    int log_level;

    /** Optional SSL certificate file (PEM). NULL disables TLS. */
    const char* cert_file;

    /** Optional SSL private key file (PEM). NULL disables TLS. */
    const char* key_file;

    /** Optional PID file path. NULL disables. */
    const char* pidfile;

} guacd_options;

/**
 * Allocate and configure a new guacd instance.
 *
 * Returns NULL on allocation failure or if the options struct is invalid.
 * The returned handle must be released with guacd_free().
 *
 * This call does NOT bind the listener socket yet - that happens inside
 * guacd_run() or guacd_run_async(). You can therefore call guacd_free()
 * on a created instance without ever running it.
 */
GUACD_API guacd_instance* guacd_create(const guacd_options* opts);

/**
 * Run the guacd listener loop in the calling thread.
 *
 * Blocks until guacd_stop() is called from another thread or a fatal
 * error occurs (failed bind, out-of-memory, etc.). Returns 0 on clean
 * shutdown, non-zero on error.
 *
 * Only one of guacd_run() or guacd_run_async() may be called on a given
 * instance. Calling guacd_run() after guacd_run_async() is undefined.
 */
GUACD_API int guacd_run(guacd_instance* instance);

/**
 * Run the guacd listener loop in a background thread owned by the
 * instance. Returns immediately.
 *
 * Returns 0 if the thread was started successfully, non-zero on error.
 * The background thread will automatically exit when guacd_stop() is
 * called or when guacd_free() is invoked.
 */
GUACD_API int guacd_run_async(guacd_instance* instance);

/**
 * Ask the listener to stop accepting new connections and shut down
 * gracefully. Existing connections are terminated.
 *
 * Safe to call from any thread, including a signal handler context
 * (sets a single atomic flag).
 *
 * This call returns immediately; the actual shutdown happens when the
 * listener thread next wakes up (at most ~1 second on Windows due to
 * the select() yield in the accept loop).
 */
GUACD_API void guacd_stop(guacd_instance* instance);

/**
 * Free a guacd instance and all its resources.
 *
 * If the instance was started via guacd_run_async(), this call will:
 *   1. Implicitly call guacd_stop()
 *   2. Wait for the background thread to exit (up to ~5 seconds)
 *   3. Release memory
 *
 * Passing NULL is a no-op.
 */
GUACD_API void guacd_free(guacd_instance* instance);

/**
 * Command-line compatibility entry point.
 *
 * Implements the same argument parsing as the standalone guacd.exe:
 *   -l PORT, -b HOST, -p PIDFILE, -L LEVEL, -C CERT, -K KEY, -f, -v
 *
 * Blocks until the listener stops. Returns the same exit code guacd.exe
 * would return (0 on clean shutdown, non-zero on error).
 *
 * Equivalent to building options from argv, calling guacd_create() +
 * guacd_run() + guacd_free().
 */
GUACD_API int guacd_main(int argc, char** argv);

/**
 * Returns the guacd version string, e.g. "1.6.1-win32".
 * The returned pointer is a static string literal, do not free it.
 */
GUACD_API const char* guacd_version(void);

#ifdef __cplusplus
}
#endif

#endif /* GUACD_API_H */
