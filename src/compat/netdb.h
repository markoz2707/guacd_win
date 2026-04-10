/*
 * POSIX netdb.h compatibility shim for Windows
 */
#ifndef COMPAT_NETDB_H
#define COMPAT_NETDB_H
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include_next <netdb.h>
#endif
#endif
