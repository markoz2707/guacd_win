/*
 * POSIX libgen.h compatibility shim for Windows
 */

#ifndef COMPAT_LIBGEN_H
#define COMPAT_LIBGEN_H

#ifdef _WIN32

/* Minimal basename/dirname for Windows */
static inline char* basename(char* path) {
    char* p = path;
    char* last = path;
    if (!path) return (char*)".";
    while (*p) {
        if (*p == '/' || *p == '\\')
            last = p + 1;
        p++;
    }
    return last;
}

#else
#include_next <libgen.h>
#endif

#endif
