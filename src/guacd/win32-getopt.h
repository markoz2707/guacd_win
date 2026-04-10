#ifndef GUACD_WIN32_GETOPT_H
#define GUACD_WIN32_GETOPT_H

#ifdef _WIN32

extern char* optarg;
extern int optind, opterr, optopt;

int getopt(int argc, char* const argv[], const char* optstring);

#endif /* _WIN32 */

#endif /* GUACD_WIN32_GETOPT_H */
