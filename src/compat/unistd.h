/*
 * POSIX unistd.h compatibility shim for Windows
 * Provides minimal definitions needed by libguac headers
 */

#ifndef COMPAT_UNISTD_H
#define COMPAT_UNISTD_H

#ifdef _WIN32

#include <io.h>
#include <process.h>
#include <direct.h>
#include <winsock2.h>

/* access() mode flags */
#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1
#endif

/* POSIX permission bits */
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif
#ifndef S_IXUSR
#define S_IXUSR 0
#endif
#ifndef S_IRWXU
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#endif
#ifndef S_IRGRP
#define S_IRGRP 0
#endif
#ifndef S_IWGRP
#define S_IWGRP 0
#endif
#ifndef S_IXGRP
#define S_IXGRP 0
#endif
#ifndef S_IROTH
#define S_IROTH 0
#endif

/* POSIX random → C stdlib */
#define srandom srand
#define random rand

/* wcwidth - not available on Windows MSVC */
#include <wchar.h>
static inline int wcwidth(wchar_t c) {
    if (c == 0) return 0;
    if (c < 32 || (c >= 0x7f && c < 0xa0)) return -1;
    /* CJK fullwidth characters */
    if ((c >= 0x1100 && c <= 0x115f) ||
        c == 0x2329 || c == 0x232a ||
        (c >= 0x2e80 && c <= 0xa4cf && c != 0x303f) ||
        (c >= 0xac00 && c <= 0xd7a3) ||
        (c >= 0xf900 && c <= 0xfaff) ||
        (c >= 0xfe10 && c <= 0xfe19) ||
        (c >= 0xfe30 && c <= 0xfe6f) ||
        (c >= 0xff00 && c <= 0xff60) ||
        (c >= 0xffe0 && c <= 0xffe6) ||
        (c >= 0x20000 && c <= 0x2fffd) ||
        (c >= 0x30000 && c <= 0x3fffd))
        return 2;
    return 1;
}

/* Standard file descriptors */
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

/* Map POSIX functions to Windows equivalents */
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define access _access
#define dup _dup
#define dup2 _dup2
#define getcwd _getcwd
#define getpid _getpid
#define isatty _isatty
#define lseek _lseek
#define rmdir _rmdir
#define unlink _unlink
#endif

/* pipe() - Windows _pipe() requires size and mode args */
#include <fcntl.h>
static inline int pipe(int fd[2]) {
    return _pipe(fd, 4096, _O_BINARY);
}

/* read/write/close for file descriptors - DO NOT define these as macros!
 * On Windows, sockets and file descriptors are separate namespaces.
 * _close() on a SOCKET handle causes undefined behavior.
 * Code should use _read/_write/_close explicitly for CRT file descriptors
 * and recv/send/closesocket for Winsock sockets. */

/* ssize_t for MSVC */
#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

/* usleep - not available on Windows, use Sleep */
static inline int usleep(unsigned int usec) {
    Sleep(usec / 1000);
    return 0;
}

/* sleep */
static inline unsigned int sleep(unsigned int seconds) {
    Sleep(seconds * 1000);
    return 0;
}

#else
/* On POSIX systems, include the real unistd.h */
#include_next <unistd.h>
#endif /* _WIN32 */

#endif /* COMPAT_UNISTD_H */
