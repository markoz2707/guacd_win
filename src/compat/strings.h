/*
 * POSIX strings.h compatibility shim for Windows
 */

#ifndef COMPAT_STRINGS_H
#define COMPAT_STRINGS_H

#ifdef _WIN32

#include <string.h>

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#else
#include_next <strings.h>
#endif

#endif
