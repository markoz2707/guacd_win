/*
 * POSIX netinet/tcp.h compatibility shim for Windows
 */
#ifndef COMPAT_NETINET_TCP_H
#define COMPAT_NETINET_TCP_H
#ifdef _WIN32
#include <winsock2.h>
#else
#include_next <netinet/tcp.h>
#endif
#endif
