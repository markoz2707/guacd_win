/*
 * POSIX sys/wait.h compatibility shim for Windows
 */
#ifndef COMPAT_SYS_WAIT_H
#define COMPAT_SYS_WAIT_H
#ifdef _WIN32
/* No waitpid on Windows - provide stubs */
#define WNOHANG 1
#define WIFEXITED(x) 1
#define WEXITSTATUS(x) (x)
static inline int waitpid(int pid, int* status, int options) {
    (void)pid; (void)status; (void)options;
    return -1;
}
#else
#include_next <sys/wait.h>
#endif
#endif
