/*
 * POSIX sys/socket.h compatibility shim for Windows
 */
#ifndef COMPAT_SYS_SOCKET_H
#define COMPAT_SYS_SOCKET_H
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include_next <sys/socket.h>
#endif
#endif
