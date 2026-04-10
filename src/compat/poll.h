/*
 * POSIX poll.h compatibility shim for Windows
 */
#ifndef COMPAT_POLL_H
#define COMPAT_POLL_H
#ifdef _WIN32
#include <winsock2.h>
/* WSAPoll is available on Vista+ */
#define poll WSAPoll
#define POLLIN  0x0001
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020
typedef struct pollfd WSAPOLLFD;
#else
#include_next <poll.h>
#endif
#endif
