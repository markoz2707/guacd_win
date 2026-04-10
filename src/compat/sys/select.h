/*
 * POSIX sys/select.h compatibility shim for Windows
 */
#ifndef COMPAT_SYS_SELECT_H
#define COMPAT_SYS_SELECT_H
#ifdef _WIN32
#include <winsock2.h>
#else
#include_next <sys/select.h>
#endif
#endif
