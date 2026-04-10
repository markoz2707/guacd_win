/*
 * POSIX netinet/in.h compatibility shim for Windows
 */
#ifndef COMPAT_NETINET_IN_H
#define COMPAT_NETINET_IN_H
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include_next <netinet/in.h>
#endif
#endif
