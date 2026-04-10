/*
 * POSIX sys/time.h compatibility shim for Windows
 */

#ifndef COMPAT_SYS_TIME_H
#define COMPAT_SYS_TIME_H

#ifdef _WIN32

#include <winsock2.h>  /* provides struct timeval */
#include <windows.h>
#include <time.h>

/* gettimeofday replacement */
struct timezone;
static inline int gettimeofday(struct timeval* tv, struct timezone* tz) {
    (void)tz;
    if (tv) {
        FILETIME ft;
        ULARGE_INTEGER li;
        GetSystemTimeAsFileTime(&ft);
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        li.QuadPart -= 116444736000000000ULL;
        tv->tv_sec = (long)(li.QuadPart / 10000000);
        tv->tv_usec = (long)((li.QuadPart % 10000000) / 10);
    }
    return 0;
}

#else
#include_next <sys/time.h>
#endif /* _WIN32 */

#endif /* COMPAT_SYS_TIME_H */
