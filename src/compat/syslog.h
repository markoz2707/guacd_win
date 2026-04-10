/*
 * POSIX syslog.h compatibility shim for Windows
 */

#ifndef COMPAT_SYSLOG_H
#define COMPAT_SYSLOG_H

#ifdef _WIN32

/* Log levels */
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

/* Options */
#define LOG_PID     0x01
#define LOG_CONS    0x02
#define LOG_NDELAY  0x08
#define LOG_NOWAIT  0x10

/* Facilities */
#define LOG_KERN    (0<<3)
#define LOG_USER    (1<<3)
#define LOG_DAEMON  (3<<3)
#define LOG_LOCAL0  (16<<3)

/* No-op implementations */
static inline void openlog(const char* ident, int option, int facility) {
    (void)ident; (void)option; (void)facility;
}
static inline void closelog(void) {}
static inline void syslog(int priority, const char* format, ...) {
    (void)priority; (void)format;
}

#else
#include_next <syslog.h>
#endif /* _WIN32 */

#endif /* COMPAT_SYSLOG_H */
