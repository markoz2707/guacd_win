# Guacd — Native Windows Port

Native Windows port of [Apache Guacamole](https://guacamole.apache.org/) `guacd` (1.6.1),
including the RDP and SSH protocol plugins. No Cygwin, WSL, MSYS2 runtime, or Docker.

Everything runs as a regular Win32 executable linked against MSVC CRT,
Winsock2, pthreads4w and vcpkg-provided dependencies.

This document is the **integration reference** — it covers the source layout,
build system, architectural changes from upstream, APIs, runtime dependencies,
and the pitfalls encountered during porting. Use it when you embed these
libraries in a larger project.

---

## Table of contents

1. [What works](#what-works)
2. [Build artifacts](#build-artifacts)
3. [Quick start](#quick-start)
4. [Build from scratch](#build-from-scratch)
5. [Runtime dependencies](#runtime-dependencies)
6. [Project layout](#project-layout)
7. [Architecture changes from upstream](#architecture-changes-from-upstream)
8. [Porting compendium](#porting-compendium-all-issues-solved)
9. [Embedding / API](#embedding--api)
10. [Web client bridge](#web-client-bridge)
11. [Known limitations](#known-limitations)
12. [Troubleshooting](#troubleshooting)

---

## What works

| Component | Status | Notes |
|---|---|---|
| `guacd.exe` daemon | ✅ Working | Thread-per-connection (no fork), Winsock, 58 KB |
| `guac-client-rdp.dll` | ✅ Working | FreeRDP 3.8.0, classic GDI path, RDPGFX opt-out |
| `guac-client-ssh.dll` | ✅ Working | libssh2, full terminal emulator |
| Hyper-V VMConnect | ✅ Working | `security=vmconnect`, port 2179, preconnection-blob |
| Clipboard (CLIPRDR) | ✅ Working | Text only, glyph/bitmap channels disabled |
| Classic RDP (`3389`) | ✅ Working | Tested by the test script without VMConnect |
| SSH with terminal | ✅ Compiles | Not end-to-end tested in this session |
| `guac-client-vnc.dll` | ❌ Not built | libvncclient not installed via vcpkg |
| `guac-client-telnet.dll` | ❌ Not built | libtelnet not installed via vcpkg |
| Audio (RDPSND) | ❌ Not built | `guac-common-svc` FreeRDP plugin not compiled |
| Drive redirection (RDPDR) | ❌ Not built | Same as above |
| SSH agent forwarding | ❌ Disabled | `ssh_agent.c` depends on a missing `ssh_key.h` |
| Printing via fork/exec | ❌ Stubbed | Returns failure on Windows, logs a warning |
| Daemon-mode (fork to background) | ❌ N/A | Always foreground; use a Windows Service wrapper if needed |

## Build artifacts

Output directory: `build18/` (or whatever you named your build directory).

| File | Size | Purpose |
|---|---|---|
| `guacd.exe` | ~58 KB | Main daemon. Listens on TCP 4822, loads protocol DLLs on demand. |
| `guac-client-rdp.dll` | ~200 KB | RDP protocol plugin. Loaded via `LoadLibrary` from guacd. |
| `guac-client-ssh.dll` | ~84 KB | SSH protocol plugin (requires terminal emulator). |
| `guac.lib` | static | libguac core — protocol, client, display, socket. |
| `guac_common.lib` | static | Common helpers — list, surface, json, clipboard, cursors. |
| `guac_common_ssh.lib` | static | Shared SSH support (used by SSH plugin and SFTP in RDP). |
| `guac_terminal.lib` | static | Terminal emulator used by SSH plugin. |
| `legacy.dll` | 108 KB | OpenSSL legacy provider — required for NTLM/MD4 auth. |
| `openssl.cnf` | text | Optional OpenSSL config (providers are also loaded programmatically). |

**Also bundled** (vcpkg runtime DLLs, ~55 files): FreeRDP, WinPR, libssh2,
pthreadVC3, Cairo, Pango, glib, gobject, freetype, harfbuzz, libpng, jpeg,
webp, OpenSSL, zlib, brotli, ...

## Quick start

```powershell
# Terminal 1 — start guacd (stays in foreground, Ctrl+C to stop)
cd "I:\AI Projects\guacd\build18"
.\guacd.exe -f -L info -b 0.0.0.0 -l 4822
```

```powershell
# Terminal 2 — web viewer (opens a browser at http://localhost:8080/)
python "I:\AI Projects\guacd\guac_web.py"
```

Edit `guac_web.py` `RDP_PARAMS` dictionary to point at a different Hyper-V VM
(change `preconnection-blob`) or a regular RDP host (change `hostname`, `port`,
set `security=any`, remove `preconnection-blob`).

`guacd` command-line flags:

```
-l PORT        listen port (default 4822)
-b HOST        bind address (default localhost — but use 0.0.0.0 on Windows,
               binding to "localhost" has timing issues with pthreads4w)
-p FILE        write PID file
-L LEVEL       trace | debug | info | warning | error
-C CERT -K KEY SSL/TLS certificate for the listener
-f             foreground (always true on Windows, flag is a no-op)
-v             print version and exit
```

## Build from scratch

### Prerequisites

1. **Visual Studio 2022 Build Tools** (or full VS 2022) with the "Desktop
   development with C++" workload. CMake and Ninja that ship with VS are fine.

2. **vcpkg** at `C:\vcpkg`:
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```

3. **Python 3.8+** (for the web bridge and tests only, not for the build).

4. **Perl** (for the RDP keymap generator). The one shipped with Git for
   Windows works.

### Install dependencies via vcpkg

```powershell
C:\vcpkg\vcpkg.exe install pthreads:x64-windows cairo:x64-windows `
    libpng:x64-windows libjpeg-turbo:x64-windows libwebp:x64-windows `
    openssl:x64-windows libssh2:x64-windows pango:x64-windows `
    libwebsockets:x64-windows
```

**Important**: do NOT install vcpkg's current FreeRDP (3.24+). It crashes in
`freerdp_connect()` with `EXCEPTION_ACCESS_VIOLATION` when used with this
codebase — the internal data model changed too far from what guacd 1.6.1
expects. Use FreeRDP **3.8.0** from a vcpkg overlay instead:

```powershell
# Export the FreeRDP 3.8.0 port files from vcpkg history
mkdir C:\vcpkg-overlay\freerdp
cd C:\vcpkg
git show 81a21c5440:ports/freerdp/vcpkg.json    > C:\vcpkg-overlay\freerdp\vcpkg.json
git show 81a21c5440:ports/freerdp/portfile.cmake > C:\vcpkg-overlay\freerdp\portfile.cmake
git checkout 81a21c5440 -- ports/freerdp/
Copy-Item ports\freerdp\*.cmake,ports\freerdp\*.patch,ports\freerdp\*.diff `
    C:\vcpkg-overlay\freerdp\ -Force
git checkout HEAD -- ports/freerdp/

# Install using the overlay
C:\vcpkg\vcpkg.exe install freerdp:x64-windows --overlay-ports=C:\vcpkg-overlay
```

### Generate RDP keymaps and channel wrappers

These files are generated from `.keymap` files by Perl scripts:

```powershell
cd "I:\AI Projects\guacd\src\protocols\rdp"
perl keymaps/generate.pl keymaps/*.keymap > _generated_keymaps.c
perl plugins/generate-entry-wrappers.pl plugins/channels.h
# Replace the top "#include \"config.h\"" line in _generated_keymaps.c
# with the conditional win32 variant so MSVC finds config.win32.h:
(Get-Content _generated_keymaps.c -Raw) -replace `
    '^#include "config\.h"', `
    "#ifdef _WIN32`n#include `"config.win32.h`"`n#else`n#include `"config.h`"`n#endif" |
    Set-Content _generated_keymaps.c
```

### Configure and build

```powershell
mkdir "I:\AI Projects\guacd\build"
cd "I:\AI Projects\guacd\build"

# Invoke via vcvars64 so cl/link/ninja are on PATH
cmd /c "`"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat`" && cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_C_COMPILER=cl -DCMAKE_BUILD_TYPE=Release && ninja"
```

CMake reports which optional components are enabled:

```
-- Found FreeRDP: C:/vcpkg/installed/x64-windows/lib/freerdp3.lib
-- Found libssh2: C:/vcpkg/installed/x64-windows/lib/libssh2.lib
-- Terminal emulator: enabled
-- common-ssh library: enabled
-- RDP protocol plugin: enabled
-- SSH protocol plugin: enabled
```

### Bundle runtime DLLs

The built `guacd.exe` needs the vcpkg-provided DLLs next to it. CMake does
this automatically via `add_custom_command(TARGET guacd POST_BUILD ...)` but
if you move the binaries, copy them manually:

```powershell
Copy-Item "C:\vcpkg\installed\x64-windows\bin\*.dll" ".\" -Force
# Also copy legacy.dll separately — it's a provider, not a regular DLL
Copy-Item "C:\vcpkg\installed\x64-windows\bin\legacy.dll" ".\" -Force
```

## Runtime dependencies

The following DLLs MUST be in the same directory as `guacd.exe` (or on
`PATH`):

```
Core runtime:
  pthreadVC3.dll          POSIX threads emulation
  ws2_32                  (Windows built-in, no DLL needed)
  rpcrt4                  (Windows built-in, no DLL needed, for UUID)

OpenSSL (from vcpkg):
  libssl-3-x64.dll
  libcrypto-3-x64.dll
  legacy.dll              Provider for MD4/NTLM — critical for Hyper-V auth

FreeRDP 3.8.0 (from vcpkg overlay):
  freerdp3.dll
  freerdp-client3.dll
  winpr3.dll

Graphics (from vcpkg):
  cairo-2.dll, pixman-1-0.dll
  libpng16.dll, zlib1.dll
  jpeg62.dll, turbojpeg.dll
  libwebp.dll, libwebpdecoder.dll, libwebpdemux.dll, libwebpmux.dll,
  libsharpyuv.dll

Terminal / fonts (for SSH plugin only):
  pango-1.0-0.dll, pangocairo-1.0-0.dll, pangoft2-1.0-0.dll, pangowin32-1.0-0.dll
  glib-2.0-0.dll, gobject-2.0-0.dll, gmodule-2.0-0.dll, gio-2.0-0.dll,
  gthread-2.0-0.dll
  freetype.dll, harfbuzz.dll, harfbuzz-raster.dll, harfbuzz-subset.dll,
  harfbuzz-vector.dll
  fontconfig-1.dll, libexpat.dll, fribidi-0.dll
  pcre2-8.dll, pcre2-16.dll, pcre2-32.dll, pcre2-posix.dll

SSH plugin only:
  libssh2.dll

Kubernetes (not built in this port):
  websockets.dll          (libwebsockets, bundled but unused)

Misc transitive:
  bz2.dll, brotlicommon.dll, brotlidec.dll, brotlienc.dll,
  cairo-gobject-2.dll, cairo-script-interpreter-2.dll,
  charset-1.dll, cjson.dll, ffi-8.dll, girepository-2.0-0.dll,
  iconv-2.dll, intl-8.dll, uv.dll, legacy.dll
```

## Project layout

```
guacd/
├── CMakeLists.txt              ← Build system (replaces autotools on Windows)
├── config.win32.h              ← Manual config.h for MSVC builds
├── PORTING-WINDOWS.md          ← This file
├── guac_web.py                 ← WebSocket bridge + HTML5 client
├── test_rdp_hyperv.py          ← Raw protocol test client
├── static/
│   └── guacamole-common.js     ← Guacamole JS client library (patched for browser)
│
├── src/
│   ├── compat/                 ← POSIX header shims for MSVC
│   │   ├── unistd.h            ← pipe, sleep, usleep, close/read/write, wcwidth,
│   │   │                         access flags, permission bits, srandom/random
│   │   ├── libgen.h, strings.h, syslog.h, poll.h
│   │   ├── sys/{socket,time,wait,select}.h
│   │   ├── netinet/{in,tcp}.h, netdb.h, arpa/inet.h
│   │
│   ├── guacd/                  ← Main daemon
│   │   ├── daemon.c            ← Accept loop, signal handling via SetConsoleCtrlHandler
│   │   ├── proc.c, proc.h      ← fork() → pthreads model
│   │   ├── connection.c        ← Per-connection thread + I/O relay
│   │   ├── move-fd.c           ← SCM_RIGHTS → integer fd passing (shared heap)
│   │   ├── conf-*.c, log.c
│   │   ├── win32-compat.h      ← Windows compat API (included in _WIN32 paths)
│   │   ├── win32-getopt.c/.h   ← getopt() for MSVC
│   │
│   ├── libguac/                ← Core library
│   │   ├── client.c            ← dlopen() → LoadLibraryA() for plugins
│   │   ├── id.c                ← uuid_generate() → UuidCreate()
│   │   ├── tcp.c               ← fcntl(NONBLOCK) → ioctlsocket(FIONBIO)
│   │   ├── socket-fd.c         ← send()/recv() with ENABLE_WINSOCK
│   │   ├── socket-wsa.c        ← Winsock-specific send/recv
│   │   ├── timestamp.c         ← gettimeofday() inline Windows impl
│   │   ├── flag.c              ← CLOCK_MONOTONIC → QueryPerformanceCounter
│   │   ├── error.c             ← #warn → #pragma message for MSVC
│   │   ├── file.c              ← openat() stub, mkdir/ftruncate, permission constants
│   │   ├── ... (~50 files patched)
│   │
│   ├── common/                 ← Common helpers (list, surface, json, ...)
│   ├── common-ssh/             ← Shared SSH library
│   ├── terminal/               ← Terminal emulator (for SSH/Telnet)
│   │
│   └── protocols/
│       ├── rdp/
│       │   ├── client.c        ← Loads OpenSSL legacy provider programmatically
│       │   ├── rdp.c           ← SEH-wrapped freerdp_connect, thread entry
│       │   ├── settings.c      ← _putenv_s instead of setenv
│       │   ├── fs.c            ← Full Win32 reimpl (FindFirstFile, GetDiskFreeSpaceEx)
│       │   ├── print-job.c     ← fork/exec stubbed out
│       │   ├── gdi.c           ← Full-surface dirty marking for refresh
│       │   ├── _generated_keymaps.c, _generated_channel_entry_wrappers.c
│       │   └── ...
│       └── ssh/
│           ├── client.c
│           ├── ssh.c           ← langinfo.h wrapped
│           └── ...             (ssh_agent.c disabled, see note)
│
└── build18/                    ← Build output + runtime DLLs
```

## Architecture changes from upstream

### 1. Process model: `fork()` → pthreads

Upstream guacd forks a child process per connection; the child loads the
protocol plugin and handles the user. Communication with the parent uses
UNIX domain socketpair + SCM_RIGHTS file-descriptor passing.

Windows has no `fork()`, so the port uses threads.

| Area | Linux / upstream | Windows port |
|---|---|---|
| Connection isolation | `fork()` child | `pthread_create()` detached thread |
| Parent↔child IPC | UNIX `socketpair(AF_UNIX, DGRAM)` | TCP loopback `socket()` pair (`win32_socketpair()` in `win32-compat.h`) |
| FD passing | `sendmsg()` with `SCM_RIGHTS` | `send()` of the integer value — threads share the process FD table |
| `proc->fd_socket` | Per-process scalar | Split into `fd_socket` (main-thread side) and `proc_fd_socket` (child-thread side) to avoid a race |
| `waitpid(child)` | Blocks until child exits | Blocking `recv()` on the proc socketpair — returns 0 when the child thread closes its end |
| `kill(-pgid, SIGTERM)` | POSIX termination | `guac_client_stop()` cooperative shutdown |

Consequence: **one crash in a plugin kills the whole daemon**. That's an
inherent tradeoff of the thread model — upstream gets per-connection
sandboxing for free from the kernel, we don't.

### 2. `accept()` + pthreads4w quirk

With pthreads4w, a simple blocking `accept()` on the listen socket
**occasionally hangs** even when TCP handshake has completed. The port uses
`select()` with a 1-second timeout *before* `accept()` as a reliable yield
point. See the accept loop in `src/guacd/daemon.c`.

### 3. Plugin loading

```c
// Linux
void* handle = dlopen("libguac-client-rdp.so", RTLD_LAZY);
void* sym = dlsym(handle, "guac_client_init");

// Windows
HMODULE handle = LoadLibraryA("guac-client-rdp.dll");
FARPROC sym = GetProcAddress(handle, "guac_client_init");
```

Plugin filename prefix and suffix are set per-platform in
`src/libguac/guacamole/plugin-constants.h`:

```c
#ifdef _WIN32
#define GUAC_PROTOCOL_LIBRARY_PREFIX "guac-client-"
#define GUAC_PROTOCOL_LIBRARY_SUFFIX ".dll"
#else
#define GUAC_PROTOCOL_LIBRARY_PREFIX "libguac-client-"
#define GUAC_PROTOCOL_LIBRARY_SUFFIX ".so"
#endif
```

DLL symbol export is enabled globally via CMake:

```cmake
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
```

Without this, `guac_client_init` is invisible to `GetProcAddress` because
MSVC doesn't add `__declspec(dllexport)` to non-class symbols.

### 4. Logging

`syslog()` is a no-op on Windows (stubs in `win32-compat.h`). All log output
goes to `stderr`, flushed after every message (`log.c` has an added
`fflush(stderr)` — without it, messages are buffered and you can't
diagnose crashes).

Use file redirection or a terminal that handles the output:

```powershell
.\guacd.exe -f -L debug 2>guacd.log      # works
```

### 5. Signal handling

No POSIX signals on Windows. Instead:

```c
SetConsoleCtrlHandler(win32_ctrl_handler, TRUE);
// win32_ctrl_handler sets stop_everything = 1 on CTRL_C_EVENT,
// CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT
```

SIGPIPE / SIGCHLD handlers are wrapped in `#ifndef _WIN32`.

### 6. POSIX `<sys/time.h>` — `gettimeofday`

Reimplemented inline in both `src/guacd/win32-compat.h` and
`src/libguac/timestamp.c` using `GetSystemTimeAsFileTime()`.

### 7. OpenSSL 3 legacy provider — **critical for Hyper-V**

OpenSSL 3 moved MD4 (used by NTLM) into the "legacy" provider, which must
be loaded explicitly. Without it, NTLM authentication fails silently with
`ERRCONNECT_ACTIVATION_TIMEOUT`.

The RDP plugin loads the legacy provider programmatically at the start of
`guac_client_init()` (see `src/protocols/rdp/client.c`):

```c
OSSL_PROVIDER_set_default_search_path(NULL, exe_directory);
OSSL_PROVIDER* legacy  = OSSL_PROVIDER_load(NULL, "legacy");
OSSL_PROVIDER* default = OSSL_PROVIDER_load(NULL, "default");
```

The `legacy.dll` file must exist next to `guacd.exe`.

### 8. FreeRDP 3.8 vs 3.24

vcpkg ships FreeRDP 3.24.1. With that version, `freerdp_connect()` crashes
with `EXCEPTION_ACCESS_VIOLATION` before even invoking the user's
`PreConnect` or `LoadChannels` callbacks. Cause: internal reshuffling of
`rdpSettings` / `rdpContext` offsets that guacd 1.6.1 doesn't know about.

**This port pins FreeRDP 3.8.0** via a vcpkg overlay. See the
"Install dependencies via vcpkg" section.

The following feature flags must be set in `config.win32.h` for FreeRDP 3.8:

```c
#define FREERDP_HAS_CONTEXT             1    // use instance->context->settings
#define HAVE_WINPR_ALIGNED              1    // winpr_aligned_malloc/free
#define HAVE_SETTERS_GETTERS            1    // freerdp_settings_set_*() instead of struct access
#define RDP_INST_HAS_LOAD_CHANNELS      1    // use LoadChannels callback
#define HAVE_FREERDP_VERIFYCERTIFICATEEX 1   // new certificate callback
#define HAVE_DISCONNECT_CONTEXT         1    // freerdp_shall_disconnect_context()
#define HAVE_FREERDPCONVERTCOLOR        1    // FreeRDPConvertColor wrapper
#define HAVE_CLIPRDR_HEADER             1    // CLIPRDR structs with common sub-struct
```

`WITH_FREERDP_DEPRECATED` is also defined from CMakeLists.txt for the RDP
plugin target so legacy `instance->settings` / `instance->update` pointers
still resolve.

### 9. Hyper-V VMConnect

Connecting to a Hyper-V VM's console (not via Remote Desktop in the guest,
but via the host's VMConnect) requires:

1. `security=vmconnect` — sets `FreeRDP_VmConnectMode=TRUE`, NLA+TLS on, ExtSec off
2. `port=2179` — Hyper-V's VMConnect listener, not 3389
3. `preconnection-blob=<VM-GUID>` — lowercase GUID of the VM, sent via the
   RDP Preconnection PDU so Hyper-V knows which VM to route to
4. `disable-auth=false` — you **must** authenticate with the current
   Windows user's Kerberos/NTLM credentials; the current user must be a
   member of `Hyper-V Administrators` (or Administrators)
5. OpenSSL legacy provider loaded — NTLM uses MD4

Get the VM GUID from an elevated PowerShell:

```powershell
Get-VM | Select-Object Name, Id, State
```

### 10. Client display — JavaScript `z-index` override

`guacamole-common-js` creates its canvases with `z-index: -1`, meaning they
will be hidden behind any non-transparent background on the parent
container. The web client in `guac_web.py` overrides this in CSS and via
a `MutationObserver` to force `z-index: auto`.

### 11. Client display — bridge buffering

The raw TCP-to-WebSocket forwarding that most examples use **will not
work** with guacamole-common-js. The JS parser expects each WebSocket
message to contain complete instructions (length-prefixed elements ending
in `;`). A naive bridge that forwards each `recv()` chunk can split an
instruction across two messages, which closes the tunnel with
`Incomplete instruction`.

`guac_web.py::split_complete_instructions()` parses length prefixes,
identifies the boundary of the last complete instruction, and only
forwards bytes up to that point. Incomplete trailing bytes stay in the
buffer until the next `recv()`.

## Porting compendium — all issues solved

Grouped by layer. Each bullet is an actual real issue that blocked the
port; the fix is in the named file.

### Process model and IPC

- **`fork()` does not exist** → replaced with `pthread_create()` in
  `src/guacd/proc.c`. The child-process branch of `guacd_create_proc()`
  becomes a thread entry point.
- **`proc->fd_socket` races** with the parent reading/writing the same
  field the thread is overwriting. Fixed by adding a second field
  `proc_fd_socket` in `src/guacd/proc.h`, one per view.
- **`close()` after `guacd_send_fd()`** destroys the socket for the child,
  because threads share the FD table. Fixed: `connection.c` wraps the
  `close()` in `#ifndef _WIN32`.
- **`sendmsg()`/`recvmsg()` with SCM_RIGHTS** doesn't exist → `move-fd.c`
  has a Windows path that just sends/receives the integer value.
- **`socketpair(AF_UNIX)`** → `win32_socketpair()` uses a TCP loopback
  listener + connect + accept.
- **`waitpid()`** → in `connection.c`, blocking `recv()` on the proc's
  socket; returns 0 when the proc thread closes its end.
- **`kill(-pgid, SIGTERM)`** → `guac_client_stop()` only.

### Sockets and I/O

- **`close()`/`read()`/`write()` conflict with CRT and Winsock** — removing
  the `#define close _close` macros from `src/compat/unistd.h` was
  critical. If `_close()` is called on a `SOCKET`, Winsock state is
  corrupted and subsequent `accept()` hangs.
- **`accept()` hangs** on pthreads4w main thread — worked around by
  calling `select()` first in `daemon.c`.
- **`SOCKET` is `UINT_PTR`**, stored as `int`. Works in practice because
  socket values are small on Windows.
- **`socklen_t`** — typedef'd as `int` in `src/compat/sys/socket.h`.
- **`poll()`** — mapped to `WSAPoll()` in `src/libguac/wait-fd.c`.
- **`fcntl(O_NONBLOCK)`** → `ioctlsocket(FIONBIO)` in
  `src/libguac/tcp.c`.

### Threads and sync

- **`pthread_mutexattr_setpshared(PROCESS_SHARED)`** fails on pthreads4w.
  Wrapped in `#ifndef _WIN32` in 6 files (`socket-fd`, `socket-ssl`,
  `socket-broadcast`, `socket-wsa`, `pool`, `flag`, `rwlock`).
- **`CLOCK_MONOTONIC`** not available → `QueryPerformanceCounter()` in
  `src/libguac/flag.c`.
- **`pthread_rwlockattr_setpshared`** — same treatment.

### Time

- **`gettimeofday()`** — inline Windows impl in
  `src/guacd/win32-compat.h` and `src/libguac/timestamp.c`.
- **`nanosleep()`** → `Sleep()`.
- **`clock_gettime(CLOCK_REALTIME, ...)`** — in `audio-buffer.c`,
  replaced with a Windows inline.

### Strings and text

- **`wcwidth()`** not in MSVC CRT → implemented in `src/compat/unistd.h`.
- **`strcasecmp`/`strncasecmp`** → `_stricmp`/`_strnicmp` in
  `src/compat/strings.h`.
- **`nl_langinfo()`** — wrapped in `#ifndef _WIN32` in `ssh/client.c`.
- **`#warn`** (GCC only) → `#pragma message` for MSVC in `libguac/error.c`.
- **`__SIZEOF_INT__`** (GCC only) — added MSVC branch (hardcoded 4) in
  `src/libguac/guacamole/string.h`.

### File system

- **`openat()`** — stub in `src/libguac/file.c` (opens by name).
- **`fcntl(F_SETLK)` file locks** — stubbed on Windows.
- **`opendir()`/`readdir()`/`closedir()`** — vcpkg provides `<dirent.h>`
  compatible header.
- **`realpath()`** → `_fullpath()`.
- **`ftruncate()`** → `_chsize_s()`.
- **`S_IRUSR`/`S_IWUSR`/`S_IRWXU`/...** → defined in
  `src/compat/unistd.h` with values matching POSIX.
- **`mode_t`** — typedef'd as `unsigned short` in
  `src/libguac/guacamole/file.h`.

### UUID / random

- **libuuid** → `UuidCreate()` from rpcrt4 in `src/libguac/id.c`.
- **`srandom`/`random`** → `srand`/`rand` in `src/compat/unistd.h`.

### Windows-specific types

- **`pid_t`** — typedef'd as `int` in `src/guacd/win32-compat.h` and
  `config.win32.h`.
- **`ssize_t`** — typedef'd as `SSIZE_T` from `<BaseTsd.h>` in
  `config.win32.h`.
- **`SHUT_RDWR`** → `SD_BOTH` in `src/guacd/win32-compat.h`.

### FreeRDP compatibility

See [Architecture changes, point 8](#8-freerdp-38-vs-324) above. Key
defines in `config.win32.h`.

### Plugin export

- **MSVC doesn't export non-class symbols from DLLs** by default, so
  `guac_client_init` wasn't visible to `GetProcAddress`. Fixed with
  `set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)` in `CMakeLists.txt`.

### OpenSSL 3 / NTLM

- **OpenSSL 3 moved MD4 to legacy provider**, which is required for
  NTLM (used by Hyper-V VMConnect). Loaded programmatically at the
  start of `guac_client_init()` in `src/protocols/rdp/client.c`.
  `legacy.dll` must be next to `guacd.exe`.

### Web client

- **`guacamole-common-js` is CommonJS**, not a browser UMD bundle.
  Trailing `module.exports = Guacamole;` crashes in the browser. Patched
  in `static/guacamole-common.js` to also expose `window.Guacamole`.
- **`z-index: -1`** on canvases hides them behind any non-transparent
  parent background. Overridden via CSS `!important` and
  `MutationObserver`.

## Embedding / API

If you're calling into libguac from your own code (linking `guac.lib` or
loading `libguac-client-*.dll`), the upstream Guacamole documentation for
1.6.1 is still accurate. All public headers are in
`src/libguac/guacamole/`.

The parts that matter for embedding:

```c
#include <guacamole/client.h>       // guac_client, guac_client_alloc, ...
#include <guacamole/protocol.h>     // guac_protocol_send_*
#include <guacamole/socket.h>       // guac_socket_open, write, flush
#include <guacamole/user.h>         // guac_user_handle_connection
#include <guacamole/plugin.h>       // guac_client_load_plugin
```

Headers under `src/libguac/` (without the `guacamole/` prefix) are
**private** — do not depend on them.

### Running a protocol plugin directly

```c
guac_client* client = guac_client_alloc();
guac_client_load_plugin(client, "rdp");    // loads guac-client-rdp.dll

// guac_client_init is now set on the client struct
client->args                // NULL-terminated list of parameter names

// Feed a socket connected to a browser/tunnel:
guac_socket* sock = guac_socket_open(your_fd);
guac_user* user = guac_user_alloc();
user->socket = sock;
user->client = client;
user->owner = 1;

guac_user_handle_connection(user, 15000000);   // 15 second timeout
```

### Writing a custom protocol plugin (DLL)

1. Create a DLL project linking against `guac.lib`.
2. Export a `guac_client_init(guac_client*, int argc, char** argv)` function.
3. Inside it, fill `client->args`, allocate per-connection state into
   `client->data`, set handler callbacks (`client->join_handler`,
   `client->free_handler`, `client->user_handler`, ...).
4. Compile to `guac-client-<protocol>.dll`. Ensure
   `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` is `ON` or the function is
   explicitly marked `__declspec(dllexport)`.
5. Place the DLL in the same directory as `guacd.exe` (or a subdirectory
   and set the plugin path via `GUAC_LIB_DIR` in `config.win32.h`).

See `src/protocols/rdp/client.c` as a reference implementation.

### Environment variables

- `HOME` — FreeRDP's `freerdp_settings_new()` fails if this is unset. The
  RDP plugin automatically sets it from `USERPROFILE` at init.
- `OPENSSL_CONF` / `OPENSSL_MODULES` — can be used to control OpenSSL
  provider loading, but the RDP plugin loads the legacy provider
  programmatically so these are not required.

## Web client bridge

`guac_web.py` is a self-contained Python script that:

1. Serves an HTML5 page at `http://localhost:8080/`
   (includes `guacamole-common-js` bundled from `static/`).
2. Accepts WebSocket connections at `ws://localhost:8081/`.
3. For each browser, opens a TCP connection to `guacd` at
   `localhost:4822`.
4. Performs the Guacamole handshake on behalf of the browser
   (`select,rdp` → `args` → `size,audio,video,image,timezone,connect` →
   `ready`).
5. Forwards data bidirectionally, buffering TCP reads until complete
   Guacamole instructions are available (so the JS parser never sees
   a partial instruction).
6. Injects RDP connection parameters from the `RDP_PARAMS` dict in the
   Python source.

For integration into a larger project, you'll likely want to:

- Replace the `RDP_PARAMS` dict with per-session config lookup (database,
  tokens, ...).
- Run behind a reverse proxy (nginx, Caddy) with TLS termination.
- Use a proper web framework instead of `http.server.BaseHTTPRequestHandler`.
- Replace `guac_web.py` entirely with the real Apache Guacamole web
  application (`guacamole.war`) — it's a Java servlet that does exactly
  the same thing but with connection management, auth, multi-user
  support, etc.

The Python bridge exists because it's ~400 lines and can be modified in
seconds. The real webapp is the recommended production path.

## Known limitations

1. **No process isolation**. A crash in any protocol plugin takes down
   the whole daemon. Mitigate by running `guacd.exe` under a supervisor
   that restarts it (NSSM, Windows Service Manager, Docker, ...).

2. **No RDPDR, RDPSND, audio input**. The `guac-common-svc` FreeRDP plugin
   isn't built (needs a separate DLL). Without it:
    - No drive redirection
    - No printer redirection
    - No audio output or input
   You'd need to compile `plugins/guac-common-svc/guac-common-svc.c` as
   a separate DLL and install it where FreeRDP expects its addins.

3. **No RDPGFX / RemoteFX decoding**. The classic GDI path works
   reliably but the fancier graphics pipeline doesn't render frames
   through our client. Set `disable-gfx=true` in your connection
   parameters (already the default in `guac_web.py`).

4. **Refresh artifacts when dragging windows**. FreeRDP occasionally
   under-reports the invalid rect. `gdi.c` works around this by marking
   the entire display surface dirty after every paint. Costs ~100% of a
   core during rapid window movement but eliminates orphan pixels.

5. **SSH agent forwarding is disabled**. `ssh_agent.c` includes a
   non-existent `ssh_key.h` header; excluded from the build in
   `CMakeLists.txt`.

6. **Printing via `fork()/exec()` is a no-op**. `print-job.c` returns
   failure on Windows.

7. **`localhost` as listen address** sometimes doesn't work on Windows
   (Winsock binds to IPv6 only or some resolver issue). Always use
   `0.0.0.0` or an explicit IPv4 address with `guacd -b`.

8. **CLIPRDR is text-only**. File clipboard / rich formats not
   supported. The CLIPRDR channel data structures changed again in
   FreeRDP 3.24, so if you upgrade FreeRDP you'll need to revisit the
   `HAVE_CLIPRDR_HEADER` / `CLIPRDR_CONST` flags.

9. **FreeRDP pinned to 3.8.0**. Upgrading to 3.24 requires significant
   additional porting of the CLIPRDR, RAIL and event-callback signatures.

## Troubleshooting

### guacd exits immediately with no output

`stderr` buffering. Make sure you're running in a terminal, not with
`> out.log`. If you need redirection, use `2>out.log` and expect
buffered output; `log.c` already calls `fflush(stderr)` after each
message but some edge cases may still bite.

### `accept()` hangs, no connections ever processed

You hit the pthreads4w `accept()` bug. Check that
`src/guacd/daemon.c` has the `select()` call before `accept()` on the
Windows path. Rebuild `guacd.exe`.

### Plugin loads but `guac_client_init` is "not found"

`CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` is not set. Check `CMakeLists.txt`
line near the top:
```cmake
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
```
Rebuild the plugin DLL.

### `freerdp_connect() SEH EXCEPTION 0xC0000005`

You have FreeRDP 3.24 installed. Downgrade to 3.8.0 via the vcpkg
overlay described in "Install dependencies via vcpkg".

### `ERRCONNECT_ACTIVATION_TIMEOUT` when connecting to Hyper-V

The most common cause is missing OpenSSL legacy provider. Verify:

1. `legacy.dll` is next to `guacd.exe`.
2. Log shows `OpenSSL legacy provider loaded (MD4/NTLM enabled)`.
3. `disable-auth=false` in connection parameters.
4. Current Windows user is a member of `Hyper-V Administrators`.
5. The VM GUID is correct (`Get-VM | Select Id`) and the VM is running.

### `ERRINFO_RPC_INITIATED_DISCONNECT` after successful connect

Another console session is already attached to the same Hyper-V VM.
Close the other VMConnect window or Hyper-V Manager and retry.

### Browser: "Incomplete instruction" tunnel error

The bridge forwarded a partial Guacamole instruction. The
`split_complete_instructions()` function in `guac_web.py` should
prevent this. If it still happens, add a print of the bytes being
sent and check for mid-instruction truncation.

### Browser: "Invalid array length" in guacamole-common.js line ~15608

Usually a consequence of "Incomplete instruction" — the parser got
confused. Same fix as above.

### Browser: "module is not defined" when loading guacamole-common.js

The file still has an unpatched `module.exports = Guacamole;` at the
end. The patched version in `static/guacamole-common.js` has:
```js
if (typeof module !== 'undefined' && module.exports) { module.exports = Guacamole; }
if (typeof window !== 'undefined') { window.Guacamole = Guacamole; }
```

### Black screen but cursor is visible and mouse/keyboard work

Canvas `z-index: -1` is hiding behind parent background. Verify the
CSS fix in `guac_web.py` HTML_CLIENT is in place:
- `#display` has **no** background color
- `#display canvas { z-index: auto !important; }`
- `MutationObserver` sets `c.style.zIndex = 'auto'` on added canvases.

### Resolution is 1024×768 instead of what I asked for

guacd computes `settings->width = optimal_width * resolution /
optimal_resolution` where `optimal_*` come from the client's `size`
instruction. If the `size` instruction isn't sent (or arrives after
`connect`), you get the RDP default. `guac_web.py::perform_handshake()`
sends `size` before `connect` — don't reorder these.

---

Generated during the native Windows port of guacd 1.6.1.
For upstream documentation see <https://guacamole.apache.org/doc/gug/>.
