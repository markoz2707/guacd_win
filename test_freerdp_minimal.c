/* Minimal FreeRDP connect test - no guacd involved.
 * Build: cl /MD /I C:\vcpkg\installed\x64-windows\include\freerdp3
 *           /I C:\vcpkg\installed\x64-windows\include\winpr3
 *           test_freerdp_minimal.c
 *           /link C:\vcpkg\installed\x64-windows\lib\freerdp3.lib
 *                 C:\vcpkg\installed\x64-windows\lib\winpr3.lib
 */
#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <freerdp/freerdp.h>
#include <freerdp/settings.h>

static BOOL my_pre_connect(freerdp* instance) {
    printf("[cb] PreConnect called\n"); fflush(stdout);
    return TRUE;
}

static BOOL my_post_connect(freerdp* instance) {
    printf("[cb] PostConnect called\n"); fflush(stdout);
    return TRUE;
}

static BOOL my_load_channels(freerdp* instance) {
    printf("[cb] LoadChannels called\n"); fflush(stdout);
    return TRUE;
}

int main(int argc, char** argv) {
    printf("=== FreeRDP 3.24 minimal connect test ===\n"); fflush(stdout);

    printf("1. freerdp_new()\n"); fflush(stdout);
    freerdp* instance = freerdp_new();
    if (!instance) { printf("FAIL\n"); return 1; }
    printf("   instance = %p\n", instance); fflush(stdout);

    printf("2. Setting callbacks\n"); fflush(stdout);
    instance->PreConnect = my_pre_connect;
    instance->PostConnect = my_post_connect;
    instance->LoadChannels = my_load_channels;

    printf("3. ContextSize = sizeof(rdpContext) = %zu\n",
        sizeof(rdpContext)); fflush(stdout);
    instance->ContextSize = sizeof(rdpContext);

    printf("4. freerdp_context_new()\n"); fflush(stdout);
    if (!freerdp_context_new(instance)) {
        printf("FAIL: freerdp_context_new\n"); return 1;
    }
    printf("   context = %p\n", instance->context); fflush(stdout);
    printf("   context->settings = %p\n", instance->context->settings); fflush(stdout);

    printf("5. Setting basic settings\n"); fflush(stdout);
    rdpSettings* settings = instance->context->settings;
    freerdp_settings_set_string(settings, FreeRDP_ServerHostname, "localhost");
    freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, 2179);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, 1024);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, 768);
    freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32);
    freerdp_settings_set_string(settings, FreeRDP_PreconnectionBlob,
        "9DEFD48B-957E-4B9F-9FBA-EB620D2B6C86");
    freerdp_settings_set_bool(settings, FreeRDP_SendPreconnectionPdu, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_Authentication, FALSE);

    printf("6. freerdp_connect()\n"); fflush(stdout);

    BOOL ok = FALSE;
    __try {
        ok = freerdp_connect(instance);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        printf("CRASH: SEH EXCEPTION 0x%08lX in freerdp_connect()\n",
            (unsigned long)code); fflush(stdout);
        return 1;
    }

    printf("7. freerdp_connect() returned %d\n", (int)ok); fflush(stdout);
    if (!ok) {
        printf("   last error: 0x%08X\n",
            freerdp_get_last_error(instance->context));
    }

    printf("8. freerdp_disconnect()\n"); fflush(stdout);
    freerdp_disconnect(instance);
    freerdp_context_free(instance);
    freerdp_free(instance);

    printf("=== Done ===\n");
    return ok ? 0 : 1;
}
