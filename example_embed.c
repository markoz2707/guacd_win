/*
 * example_embed.c - minimal example of embedding guacd.dll into a host
 * application.
 *
 * Builds against guacd.lib (the import library produced alongside
 * guacd.dll) and guacd-api.h.
 *
 * Build (from the guacd source root):
 *
 *   cl /MD /nologo ^
 *      /I src\guacd ^
 *      /DGUACD_USE_DLL ^
 *      example_embed.c ^
 *      /link build18\guacd.lib
 *
 * Run:
 *   example_embed
 *
 * guacd.dll and all runtime DLLs must be on PATH or next to the exe.
 * Press Ctrl+C to shut down cleanly.
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>

/* Tell guacd-api.h we're consuming the DLL (dllimport). */
#define GUACD_USE_DLL
#include "guacd-api.h"

static guacd_instance* g_instance = NULL;

static BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        printf("\n[host] Ctrl+C received, asking guacd to stop...\n");
        if (g_instance) guacd_stop(g_instance);
        return TRUE;
    }
    return FALSE;
}

int main(void) {
    printf("[host] guacd embedding example\n");
    printf("[host] guacd version: %s\n", guacd_version());

    /* Configure the daemon. */
    guacd_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.bind_host = "0.0.0.0";
    opts.bind_port = 4822;
    opts.log_level = 2;        /* info */

    /* Create an instance (does not bind the socket yet). */
    g_instance = guacd_create(&opts);
    if (g_instance == NULL) {
        fprintf(stderr, "[host] guacd_create failed\n");
        return 1;
    }

    /* Install Ctrl+C handler so we can shut down cleanly. */
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    /* --- OPTION A: Blocking run in current thread --- */
    printf("[host] starting guacd synchronously on 0.0.0.0:4822\n");
    printf("[host] press Ctrl+C to stop\n");
    int rc = guacd_run(g_instance);
    printf("[host] guacd_run returned %d\n", rc);

    /* --- OPTION B: Run in background (commented out)
     *
     * printf("[host] starting guacd asynchronously\n");
     * if (guacd_run_async(g_instance) != 0) {
     *     fprintf(stderr, "[host] guacd_run_async failed\n");
     *     guacd_free(g_instance);
     *     return 1;
     * }
     *
     * printf("[host] doing other work for 30 seconds...\n");
     * Sleep(30000);
     *
     * printf("[host] stopping guacd\n");
     * guacd_stop(g_instance);
     */

    guacd_free(g_instance);
    g_instance = NULL;

    printf("[host] clean exit\n");
    return rc;
}
