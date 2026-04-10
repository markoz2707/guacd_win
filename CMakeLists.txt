# CMakeLists.txt for building guacd (Apache Guacamole server daemon)
#
# This CMakeLists.txt builds guacd as a standalone executable with libguac
# and common libraries compiled as static libraries. It is designed to work
# on Windows with MSVC using vcpkg for dependency management.
#
# Example usage with vcpkg:
#   cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..

cmake_minimum_required(VERSION 3.16)
project(guacd VERSION 1.6.1 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Automatically export all symbols from DLLs on Windows
# This makes guac_client_init and other plugin symbols accessible via LoadLibrary/GetProcAddress
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ---------------------------------------------------------------------------
# Find packages (vcpkg will handle these)
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(PkgConfig)

# Try to find Cairo via pkg-config first, then fall back to manual search
if(PkgConfig_FOUND)
    pkg_check_modules(CAIRO cairo)
endif()
if(NOT CAIRO_FOUND)
    find_path(CAIRO_INCLUDE_DIRS cairo/cairo.h)
    find_library(CAIRO_LIBRARIES cairo)
    if(CAIRO_INCLUDE_DIRS AND CAIRO_LIBRARIES)
        set(CAIRO_FOUND TRUE)
    endif()
endif()

find_package(PNG)
find_package(JPEG)
find_package(OpenSSL)
find_package(WebP CONFIG)

# ---------------------------------------------------------------------------
# libguac static library
# ---------------------------------------------------------------------------
set(LIBGUAC_SOURCES
    src/libguac/argv.c
    src/libguac/audio.c
    src/libguac/client.c
    src/libguac/display.c
    src/libguac/display-builtin-cursors.c
    src/libguac/display-cursor.c
    src/libguac/display-flush.c
    src/libguac/display-layer.c
    src/libguac/display-layer-list.c
    src/libguac/display-plan.c
    src/libguac/display-plan-combine.c
    src/libguac/display-plan-rect.c
    src/libguac/display-plan-search.c
    src/libguac/display-render-thread.c
    src/libguac/display-worker.c
    src/libguac/encode-jpeg.c
    src/libguac/encode-png.c
    src/libguac/encode-webp.c
    src/libguac/error.c
    src/libguac/fifo.c
    src/libguac/file.c
    src/libguac/fips.c
    src/libguac/flag.c
    src/libguac/hash.c
    src/libguac/id.c
    src/libguac/mem.c
    src/libguac/palette.c
    src/libguac/parser.c
    src/libguac/pool.c
    src/libguac/protocol.c
    src/libguac/raw_encoder.c
    src/libguac/recording.c
    src/libguac/rect.c
    src/libguac/rwlock.c
    src/libguac/socket.c
    src/libguac/socket-broadcast.c
    src/libguac/socket-fd.c
    src/libguac/socket-nest.c
    src/libguac/socket-tee.c
    src/libguac/string.c
    src/libguac/tcp.c
    src/libguac/timestamp.c
    src/libguac/unicode.c
    src/libguac/user.c
    src/libguac/user-handlers.c
    src/libguac/user-handshake.c
    src/libguac/wait-fd.c
    src/libguac/wol.c
)

# Add platform-specific socket sources
if(WIN32)
    list(APPEND LIBGUAC_SOURCES src/libguac/socket-wsa.c)
endif()

# Conditionally add SSL source
if(OpenSSL_FOUND)
    list(APPEND LIBGUAC_SOURCES src/libguac/socket-ssl.c)
endif()

add_library(guac STATIC ${LIBGUAC_SOURCES})

target_include_directories(guac PUBLIC
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/compat>
    ${CMAKE_SOURCE_DIR}/src/libguac
    ${CMAKE_SOURCE_DIR}
)

# Platform defines and link libraries for libguac
if(WIN32)
    target_compile_definitions(guac PRIVATE _WIN32 ENABLE_WINSOCK)
    # Use pthreads4w from vcpkg (pthreadVC3 is the MSVC-compatible variant)
    find_library(PTHREADS4W_LIBRARY NAMES pthreadVC3 pthreadVCE3 pthreadVSE3)
    if(PTHREADS4W_LIBRARY)
        target_link_libraries(guac PUBLIC ws2_32 ${PTHREADS4W_LIBRARY})
    else()
        target_link_libraries(guac PUBLIC ws2_32 Threads::Threads)
    endif()
else()
    target_link_libraries(guac PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

# Cairo
if(CAIRO_FOUND)
    target_include_directories(guac PRIVATE ${CAIRO_INCLUDE_DIRS})
    target_link_libraries(guac PUBLIC ${CAIRO_LIBRARIES})
    target_compile_definitions(guac PRIVATE HAVE_CAIRO)
endif()

# PNG
if(PNG_FOUND)
    target_link_libraries(guac PRIVATE PNG::PNG)
    target_compile_definitions(guac PRIVATE HAVE_PNG)
endif()

# JPEG
if(JPEG_FOUND)
    target_link_libraries(guac PRIVATE JPEG::JPEG)
    target_compile_definitions(guac PRIVATE HAVE_JPEG)
endif()

# WebP
if(TARGET WebP::webp)
    target_link_libraries(guac PRIVATE WebP::webp)
    target_compile_definitions(guac PRIVATE HAVE_WEBP)
endif()

# OpenSSL
if(OpenSSL_FOUND)
    target_link_libraries(guac PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(guac PUBLIC ENABLE_SSL)
endif()

# UUID - on Windows use rpcrt4, on Unix use libuuid
if(WIN32)
    target_link_libraries(guac PUBLIC rpcrt4)
else()
    find_library(UUID_LIBRARY uuid)
    if(UUID_LIBRARY)
        target_link_libraries(guac PUBLIC ${UUID_LIBRARY})
    endif()
endif()

# ---------------------------------------------------------------------------
# common static library
# ---------------------------------------------------------------------------
set(COMMON_SOURCES
    src/common/blank_cursor.c
    src/common/clipboard.c
    src/common/cursor.c
    src/common/dot_cursor.c
    src/common/ibar_cursor.c
    src/common/iconv.c
    src/common/io.c
    src/common/json.c
    src/common/list.c
    src/common/pointer_cursor.c
    src/common/rect.c
    src/common/string.c
    src/common/surface.c
)

add_library(guac_common STATIC ${COMMON_SOURCES})

target_include_directories(guac_common PUBLIC
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/compat>
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/libguac
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(guac_common PUBLIC guac)

if(CAIRO_FOUND)
    target_include_directories(guac_common PRIVATE ${CAIRO_INCLUDE_DIRS})
endif()

if(WIN32)
    target_compile_definitions(guac_common PRIVATE _WIN32)
endif()

# ---------------------------------------------------------------------------
# guacd executable
# ---------------------------------------------------------------------------
set(GUACD_SOURCES
    src/guacd/daemon.c
    src/guacd/proc.c
    src/guacd/proc-map.c
    src/guacd/connection.c
    src/guacd/conf-args.c
    src/guacd/conf-file.c
    src/guacd/conf-parse.c
    src/guacd/log.c
    src/guacd/move-fd.c
)

if(WIN32)
    list(APPEND GUACD_SOURCES src/guacd/win32-getopt.c)
endif()

add_executable(guacd ${GUACD_SOURCES})

target_include_directories(guacd PRIVATE
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/compat>
    ${CMAKE_SOURCE_DIR}/src/guacd
    ${CMAKE_SOURCE_DIR}/src/libguac
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(guacd PRIVATE guac guac_common Threads::Threads)

if(WIN32)
    target_compile_definitions(guacd PRIVATE _WIN32)
    target_link_libraries(guacd PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# guacd shared library (guacd.dll) for embedding into other applications
#
# Uses the same sources as the executable. daemon.c contains a thin main()
# wrapper at the bottom that is compiled out when GUACD_BUILD_DLL is defined,
# leaving only the exported API (guacd_main, guacd_create, guacd_run,
# guacd_run_async, guacd_stop, guacd_free, guacd_version).
# ---------------------------------------------------------------------------
add_library(guacd_shared SHARED ${GUACD_SOURCES})

set_target_properties(guacd_shared PROPERTIES
    OUTPUT_NAME "guacd"
    PREFIX ""
    # On Windows this produces guacd.dll + guacd.lib (import library).
    # On Linux this would produce libguacd.so.
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)

# GUACD_BUILD_DLL hides main() and enables __declspec(dllexport) on the
# API functions via the GUACD_API macro in guacd-api.h.
target_compile_definitions(guacd_shared PRIVATE GUACD_BUILD_DLL GUACD_EXPORTS)

target_include_directories(guacd_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/guacd>
)
target_include_directories(guacd_shared PRIVATE
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/compat>
    ${CMAKE_SOURCE_DIR}/src/libguac
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(guacd_shared PRIVATE guac guac_common Threads::Threads)

if(WIN32)
    target_compile_definitions(guacd_shared PRIVATE _WIN32)
    target_link_libraries(guacd_shared PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# Protocol dependencies discovery
# ---------------------------------------------------------------------------

# FreeRDP (for RDP protocol)
# FreeRDP 3.x installs headers under freerdp3/ and winpr3/ subdirs
find_path(FREERDP_INCLUDE_DIR freerdp/freerdp.h PATH_SUFFIXES freerdp3)
find_path(WINPR_INCLUDE_DIR winpr/winpr.h PATH_SUFFIXES winpr3)
find_library(FREERDP_LIBRARY NAMES freerdp3 freerdp)
find_library(FREERDP_CLIENT_LIBRARY NAMES freerdp-client3 freerdp-client)
find_library(WINPR_LIBRARY NAMES winpr3 winpr)
if(FREERDP_INCLUDE_DIR AND FREERDP_LIBRARY AND WINPR_LIBRARY)
    set(FREERDP_FOUND TRUE)
    message(STATUS "Found FreeRDP: ${FREERDP_LIBRARY}")
    message(STATUS "  Include: ${FREERDP_INCLUDE_DIR}")
endif()

# libssh2 (for SSH protocol)
find_path(LIBSSH2_INCLUDE_DIR libssh2.h)
find_library(LIBSSH2_LIBRARY NAMES libssh2 ssh2)
if(LIBSSH2_INCLUDE_DIR AND LIBSSH2_LIBRARY)
    set(LIBSSH2_FOUND TRUE)
    message(STATUS "Found libssh2: ${LIBSSH2_LIBRARY}")
endif()

# Pango/PangoCairo (for terminal emulator)
if(PkgConfig_FOUND)
    pkg_check_modules(PANGO pango pangocairo)
endif()
if(NOT PANGO_FOUND)
    find_path(PANGO_INCLUDE_DIRS pango/pango.h PATH_SUFFIXES pango-1.0)
    find_library(PANGO_LIB pango-1.0)
    find_library(PANGOCAIRO_LIB pangocairo-1.0)
    find_path(GLIB_INCLUDE_DIR glib.h PATH_SUFFIXES glib-2.0)
    find_path(GLIBCONFIG_INCLUDE_DIR glibconfig.h
        PATH_SUFFIXES glib-2.0/include lib/glib-2.0/include)
    find_library(GLIB_LIBRARY NAMES glib-2.0)
    find_library(GOBJECT_LIBRARY NAMES gobject-2.0)
    if(PANGO_LIB AND PANGOCAIRO_LIB)
        set(PANGO_FOUND TRUE)
        set(PANGO_LIBRARIES ${PANGO_LIB} ${PANGOCAIRO_LIB}
            ${GLIB_LIBRARY} ${GOBJECT_LIBRARY})
        set(PANGO_INCLUDE_DIRS ${PANGO_INCLUDE_DIRS}
            ${GLIB_INCLUDE_DIR} ${GLIBCONFIG_INCLUDE_DIR})
        message(STATUS "Found Pango: ${PANGO_LIB}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Terminal emulator library (needed by SSH, Telnet, Kubernetes protocols)
# ---------------------------------------------------------------------------
if(PANGO_FOUND AND CAIRO_FOUND)
    set(TERMINAL_SOURCES
        src/terminal/buffer.c
        src/terminal/char-mappings.c
        src/terminal/color-scheme.c
        src/terminal/common.c
        src/terminal/display.c
        src/terminal/named-colors.c
        src/terminal/palette.c
        src/terminal/scrollbar.c
        src/terminal/select.c
        src/terminal/selection-point.c
        src/terminal/terminal.c
        src/terminal/terminal-handlers.c
        src/terminal/terminal-stdin-stream.c
        src/terminal/typescript.c
        src/terminal/xparsecolor.c
    )
    add_library(guac_terminal STATIC ${TERMINAL_SOURCES})
    # Find harfbuzz (required by Pango headers)
    find_path(HARFBUZZ_INCLUDE_DIR hb.h PATH_SUFFIXES harfbuzz)
    # pangocairo.h includes bare "cairo.h" - ensure cairo/ subdir is in path
    find_path(CAIRO_SUBDIR_INCLUDE cairo.h PATH_SUFFIXES cairo)
    target_include_directories(guac_terminal PUBLIC
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/compat>
        ${CMAKE_SOURCE_DIR}/src/terminal
        ${CMAKE_SOURCE_DIR}/src/libguac
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}
        ${PANGO_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${CAIRO_SUBDIR_INCLUDE}
        ${HARFBUZZ_INCLUDE_DIR}
    )
    target_link_libraries(guac_terminal PUBLIC guac guac_common
        ${PANGO_LIBRARIES} ${CAIRO_LIBRARIES})
    if(WIN32)
        target_compile_definitions(guac_terminal PRIVATE _WIN32)
    endif()
    set(TERMINAL_FOUND TRUE)
    message(STATUS "Terminal emulator: enabled")
else()
    set(TERMINAL_FOUND FALSE)
    message(STATUS "Terminal emulator: disabled (missing Pango or Cairo)")
endif()

# ---------------------------------------------------------------------------
# common-ssh library (shared by SSH and SFTP protocols)
# ---------------------------------------------------------------------------
if(LIBSSH2_FOUND AND OpenSSL_FOUND)
    set(COMMON_SSH_SOURCES
        src/common-ssh/buffer.c
        src/common-ssh/key.c
        src/common-ssh/sftp.c
        src/common-ssh/ssh.c
        src/common-ssh/user.c
    )
    add_library(guac_common_ssh STATIC ${COMMON_SSH_SOURCES})
    target_include_directories(guac_common_ssh PUBLIC
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/compat>
        ${CMAKE_SOURCE_DIR}/src/common-ssh
        ${CMAKE_SOURCE_DIR}/src/libguac
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}
        ${LIBSSH2_INCLUDE_DIR}
    )
    target_link_libraries(guac_common_ssh PUBLIC guac guac_common
        ${LIBSSH2_LIBRARY} OpenSSL::SSL OpenSSL::Crypto)
    if(WIN32)
        target_compile_definitions(guac_common_ssh PRIVATE _WIN32)
        target_link_libraries(guac_common_ssh PUBLIC ws2_32)
    endif()
    set(COMMON_SSH_FOUND TRUE)
    message(STATUS "common-ssh library: enabled")
else()
    set(COMMON_SSH_FOUND FALSE)
    message(STATUS "common-ssh library: disabled (missing libssh2 or OpenSSL)")
endif()

# ---------------------------------------------------------------------------
# RDP protocol plugin (guac-client-rdp.dll)
# ---------------------------------------------------------------------------
if(FREERDP_FOUND)
    set(RDP_SOURCES
        src/protocols/rdp/argv.c
        src/protocols/rdp/beep.c
        src/protocols/rdp/client.c
        src/protocols/rdp/color.c
        src/protocols/rdp/decompose.c
        src/protocols/rdp/download.c
        src/protocols/rdp/error.c
        src/protocols/rdp/fs.c
        src/protocols/rdp/gdi.c
        src/protocols/rdp/input.c
        src/protocols/rdp/input-queue.c
        src/protocols/rdp/keyboard.c
        src/protocols/rdp/keymap.c
        src/protocols/rdp/log.c
        src/protocols/rdp/ls.c
        src/protocols/rdp/pointer.c
        src/protocols/rdp/print-job.c
        src/protocols/rdp/rdp.c
        src/protocols/rdp/resolution.c
        src/protocols/rdp/settings.c
        src/protocols/rdp/unicode.c
        src/protocols/rdp/upload.c
        src/protocols/rdp/user.c
        src/protocols/rdp/channels/audio-input/audio-buffer.c
        src/protocols/rdp/channels/audio-input/audio-input.c
        src/protocols/rdp/channels/cliprdr.c
        src/protocols/rdp/channels/common-svc.c
        src/protocols/rdp/channels/disp.c
        src/protocols/rdp/channels/pipe-svc.c
        src/protocols/rdp/channels/rail.c
        src/protocols/rdp/channels/rdpdr/rdpdr.c
        src/protocols/rdp/channels/rdpdr/rdpdr-fs.c
        src/protocols/rdp/channels/rdpdr/rdpdr-fs-messages.c
        src/protocols/rdp/channels/rdpdr/rdpdr-fs-messages-dir-info.c
        src/protocols/rdp/channels/rdpdr/rdpdr-fs-messages-file-info.c
        src/protocols/rdp/channels/rdpdr/rdpdr-fs-messages-vol-info.c
        src/protocols/rdp/channels/rdpdr/rdpdr-messages.c
        src/protocols/rdp/channels/rdpdr/rdpdr-printer.c
        src/protocols/rdp/channels/rdpei.c
        src/protocols/rdp/channels/rdpgfx.c
        src/protocols/rdp/channels/rdpsnd/rdpsnd.c
        src/protocols/rdp/channels/rdpsnd/rdpsnd-messages.c
        src/protocols/rdp/_generated_keymaps.c
        src/protocols/rdp/_generated_channel_entry_wrappers.c
        src/protocols/rdp/plugins/channels.c
        src/protocols/rdp/plugins/ptr-string.c
    )
    # SFTP support in RDP requires common-ssh
    if(COMMON_SSH_FOUND)
        list(APPEND RDP_SOURCES src/protocols/rdp/sftp.c)
    endif()

    add_library(guac-client-rdp SHARED ${RDP_SOURCES})
    target_include_directories(guac-client-rdp PRIVATE
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/compat>
        ${CMAKE_SOURCE_DIR}/src/protocols/rdp
        ${CMAKE_SOURCE_DIR}/src/libguac
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}
        ${FREERDP_INCLUDE_DIR}
        ${WINPR_INCLUDE_DIR}
    )
    target_link_libraries(guac-client-rdp PRIVATE guac guac_common
        ${FREERDP_LIBRARY} ${WINPR_LIBRARY})
    if(FREERDP_CLIENT_LIBRARY)
        target_link_libraries(guac-client-rdp PRIVATE ${FREERDP_CLIENT_LIBRARY})
    endif()
    # OpenSSL crypto for OSSL_PROVIDER_load (legacy provider for MD4/NTLM)
    if(OpenSSL_FOUND)
        target_link_libraries(guac-client-rdp PRIVATE OpenSSL::Crypto)
    endif()
    if(COMMON_SSH_FOUND)
        target_link_libraries(guac-client-rdp PRIVATE guac_common_ssh)
        target_compile_definitions(guac-client-rdp PRIVATE ENABLE_COMMON_SSH)
    endif()
    # Enable FreeRDP deprecated API for compatibility with guacd 1.6.1
    target_compile_definitions(guac-client-rdp PRIVATE WITH_FREERDP_DEPRECATED)
    if(WIN32)
        target_compile_definitions(guac-client-rdp PRIVATE _WIN32)
        target_link_libraries(guac-client-rdp PRIVATE ws2_32 shlwapi)
    endif()
    # Output name without "lib" prefix on all platforms
    set_target_properties(guac-client-rdp PROPERTIES PREFIX "")
    message(STATUS "RDP protocol plugin: enabled")
else()
    message(STATUS "RDP protocol plugin: disabled (FreeRDP not found)")
endif()

# ---------------------------------------------------------------------------
# SSH protocol plugin (guac-client-ssh.dll)
# ---------------------------------------------------------------------------
if(COMMON_SSH_FOUND AND TERMINAL_FOUND)
    set(SSH_SOURCES
        src/protocols/ssh/argv.c
        src/protocols/ssh/client.c
        src/protocols/ssh/clipboard.c
        src/protocols/ssh/input.c
        src/protocols/ssh/pipe.c
        src/protocols/ssh/settings.c
        src/protocols/ssh/sftp.c
        src/protocols/ssh/ssh.c
        # src/protocols/ssh/ssh_agent.c  # Disabled: requires internal ssh_key.h
        src/protocols/ssh/ttymode.c
        src/protocols/ssh/user.c
    )
    add_library(guac-client-ssh SHARED ${SSH_SOURCES})
    target_include_directories(guac-client-ssh PRIVATE
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/compat>
        ${CMAKE_SOURCE_DIR}/src/protocols/ssh
        ${CMAKE_SOURCE_DIR}/src/terminal
        ${CMAKE_SOURCE_DIR}/src/common-ssh
        ${CMAKE_SOURCE_DIR}/src/libguac
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}
        ${LIBSSH2_INCLUDE_DIR}
        ${PANGO_INCLUDE_DIRS}
    )
    target_link_libraries(guac-client-ssh PRIVATE guac guac_common
        guac_terminal guac_common_ssh ${LIBSSH2_LIBRARY})
    if(WIN32)
        target_compile_definitions(guac-client-ssh PRIVATE _WIN32)
        target_link_libraries(guac-client-ssh PRIVATE ws2_32)
    endif()
    set_target_properties(guac-client-ssh PROPERTIES PREFIX "")
    message(STATUS "SSH protocol plugin: enabled")
else()
    message(STATUS "SSH protocol plugin: disabled (missing libssh2, terminal, or OpenSSL)")
endif()

# ---------------------------------------------------------------------------
# Post-build: copy DLLs to output directory
# ---------------------------------------------------------------------------
if(WIN32)
    # Copy vcpkg runtime DLLs next to guacd.exe
    file(GLOB VCPKG_DLLS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
    if(VCPKG_DLLS)
        add_custom_command(TARGET guacd POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${VCPKG_DLLS}
                $<TARGET_FILE_DIR:guacd>
            COMMENT "Copying vcpkg runtime DLLs...")
    endif()
endif()
