#ifndef GUACD_CONFIG_WIN32_H
#define GUACD_CONFIG_WIN32_H

/* Version info */
#define VERSION "1.6.1-win32"
#define PACKAGE_VERSION "1.6.1-win32"

/* Windows-specific settings */
#define ENABLE_WINSOCK 1

/* No daemonization on Windows */
/* #undef HAVE_PRCTL */

/* Config file location for Windows */
#define GUACD_CONF_FILE "guacd.conf"

/* Library directory */
#define GUAC_LIB_DIR "."

/* Thread stack size */
#define GUACD_THREAD_STACK_SIZE 8388608

/* OpenSSL settings - enable if OpenSSL is available */
/* #define ENABLE_SSL 1 */
/* #undef OPENSSL_REQUIRES_THREADING_CALLBACKS */

/* Have poll() */
#define HAVE_POLL 1

/* Disable features not available on Windows */
/* #undef HAVE_CLOCK_GETTIME */
/* #undef HAVE_DECL_PTHREAD_SETATTR_DEFAULT_NP */

/* FreeRDP 3.x - context is a member of freerdp struct */
#define FREERDP_HAS_CONTEXT 1

/* FreeRDP 3.x uses winpr_aligned_malloc/free */
#define HAVE_WINPR_ALIGNED 1

/* FreeRDP 3.x: rdpSettings is opaque, must use freerdp_settings_set_*() / get_*() */
#define HAVE_SETTERS_GETTERS 1

/* FreeRDP 3.x has LoadChannels callback on rdpInstance (use instead of PreConnect hook) */
#define RDP_INST_HAS_LOAD_CHANNELS 1

/* FreeRDP 3.x uses VerifyCertificateEx callback (old VerifyCertificate is deprecated) */
#define HAVE_FREERDP_VERIFYCERTIFICATEEX 1

/* FreeRDP 3.x uses freerdp_shall_disconnect_context() (old freerdp_shall_disconnect is deprecated) */
#define HAVE_DISCONNECT_CONTEXT 1

/* FreeRDP 3.x provides FreeRDPConvertColor() wrapper */
#define HAVE_FREERDPCONVERTCOLOR 1

/* FreeRDP 3.x CLIPRDR structs use CLIPRDR_HEADER common sub-struct */
#define HAVE_CLIPRDR_HEADER 1

/* CLIPRDR/RAIL callbacks use non-const params in 3.8 */
/* #define FREERDP_CLIPRDR_CALLBACKS_REQUIRE_CONST 1 */
/* #define FREERDP_RAIL_CALLBACKS_REQUIRE_CONST 1 */

/* POSIX types not available on Windows */
#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int pid_t;
#endif

/* GUAC_RETRY_EINTR - Windows doesn't use EINTR the same way */
#ifndef GUAC_RETRY_EINTR
#define GUAC_RETRY_EINTR(result, expr) do { result = (expr); } while(0)
#endif

/* Suppress MSVC warnings for POSIX function names */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

/* Winsock must be included before windows.h */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#endif /* GUACD_CONFIG_WIN32_H */
