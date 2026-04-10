/*
 * POSIX arpa/inet.h compatibility shim for Windows
 */
#ifndef COMPAT_ARPA_INET_H
#define COMPAT_ARPA_INET_H
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include_next <arpa/inet.h>
#endif
#endif
