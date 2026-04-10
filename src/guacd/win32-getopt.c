/*
 * win32-getopt.c - A minimal getopt() implementation for Windows.
 *
 * This is a simple public-domain-style implementation of getopt()
 * suitable for parsing the guacd option string "l:b:p:L:C:K:fv".
 *
 * It supports:
 *   - Options with required arguments (indicated by ':' after the option char)
 *   - Setting optarg for options that take arguments
 *   - Proper optind tracking
 *   - Error reporting via opterr/optopt
 *   - Returning '?' for unknown options or missing arguments
 *   - Stopping at "--" or the first non-option argument
 */

#ifdef _WIN32

#include <stdio.h>
#include <string.h>

char* optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int getopt(int argc, char* const argv[], const char* optstring) {
    static int sp = 1;
    int c;
    const char* cp;

    /* Reset optarg each call */
    optarg = NULL;

    /* Check if we've exhausted arguments */
    if (optind >= argc)
        return -1;

    /* Check for non-option or end-of-options marker */
    if (argv[optind] == NULL || argv[optind][0] != '-' || argv[optind][1] == '\0')
        return -1;

    if (argv[optind][0] == '-' && argv[optind][1] == '-' && argv[optind][2] == '\0') {
        /* "--" marks end of options */
        optind++;
        return -1;
    }

    /* Get the current option character */
    c = argv[optind][sp];
    optopt = c;

    /* Look up the option in optstring */
    cp = strchr(optstring, c);
    if (cp == NULL || c == ':') {
        /* Unknown option */
        if (opterr)
            fprintf(stderr, "%s: unknown option '-%c'\n", argv[0], c);

        /* Advance to next argv element if we've consumed all chars in this one */
        if (argv[optind][++sp] == '\0') {
            optind++;
            sp = 1;
        }
        return '?';
    }

    /* Check if this option requires an argument */
    if (cp[1] == ':') {
        /* Option requires an argument */
        if (argv[optind][sp + 1] != '\0') {
            /* Argument is the rest of the current argv element */
            optarg = &argv[optind][sp + 1];
        }
        else if (optind + 1 < argc) {
            /* Argument is the next argv element */
            optarg = argv[++optind];
        }
        else {
            /* Missing required argument */
            if (opterr)
                fprintf(stderr, "%s: option '-%c' requires an argument\n", argv[0], c);
            optind++;
            sp = 1;
            return (optstring[0] == ':') ? ':' : '?';
        }
        optind++;
        sp = 1;
    }
    else {
        /* Option does not take an argument */
        if (argv[optind][++sp] == '\0') {
            optind++;
            sp = 1;
        }
    }

    return c;
}

#endif /* _WIN32 */
