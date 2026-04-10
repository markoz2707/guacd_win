#!/usr/bin/env python3
"""
Minimal WebSocket-to-guacd bridge + HTML5 Guacamole client.

Starts:
  - HTTP server on :8080 serving the web client HTML
  - WebSocket server on :8081 tunneling to guacd on localhost:4822

Opens the browser so you can see the RDP console.

Usage:
    1. Start guacd: guacd.exe -f -L info -b 0.0.0.0 -l 4822
    2. Run this:   python guac_web.py
    3. Browser opens automatically at http://localhost:8080/
"""

import asyncio
import os
import socket
import threading
import webbrowser
import http.server
import socketserver
import websockets

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
STATIC_DIR = os.path.join(SCRIPT_DIR, "static")

# ---- Configuration ----
GUACD_HOST = "localhost"
GUACD_PORT = 4822
HTTP_PORT  = 8080
WS_PORT    = 8081

# RDP / Hyper-V connection parameters
RDP_PARAMS = {
    "hostname":            "localhost",
    "port":                "2179",
    "width":               "1600",
    "height":              "900",
    "dpi":                 "96",
    "color-depth":         "32",
    "security":            "vmconnect",
    "ignore-cert":         "true",
    "disable-auth":        "false",   # Hyper-V uses Windows Kerberos/NTLM
    "preconnection-blob":  "9defd48b-957e-4b9f-9fba-eb620d2b6c86",
    "resize-method":       "none",    # No dynamic resize; avoids DISP channel timing issues
    "enable-wallpaper":    "false",
    "enable-theming":      "true",
    "enable-font-smoothing": "true",
    "enable-drive":        "false",
    "enable-printing":     "false",
    "console":             "false",
    "disable-gfx":         "true",    # Force classic RDP drawing (RDPGFX/RemoteFX path may not render)
    "color-depth":         "16",      # 16bpp without GFX is most compatible
    # Fix "orphan pixels" when dragging/resizing windows - disable caching
    # which can cause stale bitmaps to linger on screen.
    "disable-bitmap-caching":    "true",
    "disable-offscreen-caching": "true",
    "disable-glyph-caching":     "true",
    "force-lossless":            "true",
}

# ---- HTML Client ----
HTML_CLIENT = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Guacamole Web Client - Hyper-V RDP</title>
<style>
    html, body {
        margin: 0; padding: 0; height: 100%;
        background: #222; color: #eee;
        font-family: -apple-system, Segoe UI, sans-serif;
    }
    #header {
        padding: 8px 16px; background: #1a1a1a;
        border-bottom: 1px solid #333;
    }
    #status { color: #8f8; }
    #status.error { color: #f88; }
    #display {
        position: relative;
        width: 100%;
        height: calc(100vh - 45px);
        overflow: auto;
        /* NO background color! Guacamole canvases have z-index: -1
         * which places them BEHIND the parent's background. */
        cursor: default;
        display: flex;
        align-items: flex-start;
        justify-content: center;
    }
    /* Guacamole.Display inserts a div wrapper with inline style. The
     * inner canvas elements get z-index: -1 by default which means
     * they'd be invisible if the parent has any background. Override. */
    #display > div {
        position: relative !important;
        display: block !important;
        background: #000;
    }
    #display canvas {
        display: block;
        z-index: auto !important;   /* DO NOT let z-index:-1 hide us */
    }
</style>
</head>
<body>
<div id="header">
    <strong>guacd Hyper-V RDP client</strong> &mdash;
    <span id="status">Loading Guacamole JS client...</span>
</div>
<div id="display"></div>

<!-- Load guacamole-common-js served locally by this bridge -->
<script src="/static/guacamole-common.js"></script>
<script>
const statusEl = document.getElementById('status');
const displayEl = document.getElementById('display');

function setStatus(msg, isError) {
    statusEl.textContent = msg;
    statusEl.className = isError ? 'error' : '';
    console.log('[status]', msg);
}

if (typeof Guacamole === 'undefined') {
    setStatus('ERROR: Failed to load guacamole-common-js from CDN', true);
} else {
    setStatus('Connecting to WebSocket bridge...');

    // Create WebSocket tunnel to our bridge
    const wsUrl = 'ws://' + location.hostname + ':""" + str(WS_PORT) + """/';
    const tunnel = new Guacamole.WebSocketTunnel(wsUrl);

    // Create client wrapping the tunnel
    const client = new Guacamole.Client(tunnel);

    // Mount display in DOM
    const display = client.getDisplay();
    const displayRoot = display.getElement();
    displayEl.appendChild(displayRoot);

    // Instrument the display so we can see whether it ever receives size
    display.onresize = function(width, height) {
        console.log('[display] onresize', width, 'x', height);
        // Force the div to actual size so layout is visible
        displayRoot.style.width = width + 'px';
        displayRoot.style.height = height + 'px';
        displayRoot.style.minWidth = width + 'px';
        displayRoot.style.minHeight = height + 'px';
        setStatus('Display: ' + width + 'x' + height);

        // Dump the DOM so we can see what's actually mounted
        console.log('[display] root element:', displayRoot);
        console.log('[display] canvas count:',
            displayRoot.querySelectorAll('canvas').length);
        displayRoot.querySelectorAll('canvas').forEach((c, i) => {
            // FORCE visible z-index. Guacamole sets z-index: -1 which
            // hides canvas behind any non-transparent parent background.
            c.style.zIndex = 'auto';
            console.log('  canvas[' + i + ']',
                c.width + 'x' + c.height,
                'style:', c.style.cssText);
        });
    };

    // Also override z-index on any canvas added later via MutationObserver
    const mo = new MutationObserver((mutations) => {
        for (const m of mutations) {
            m.addedNodes.forEach(node => {
                if (node.tagName === 'CANVAS') {
                    node.style.zIndex = 'auto';
                } else if (node.querySelectorAll) {
                    node.querySelectorAll('canvas').forEach(c => {
                        c.style.zIndex = 'auto';
                    });
                }
            });
        }
    });
    mo.observe(displayRoot, { childList: true, subtree: true });

    // Error handling
    client.onerror = function(error) {
        setStatus('Client error: ' + (error.message || error), true);
        console.error('Client error:', error);
    };
    tunnel.onerror = function(error) {
        setStatus('Tunnel error: ' + (error.message || error), true);
        console.error('Tunnel error:', error);
    };

    // Log every incoming instruction
    const origOnInstruction = tunnel.oninstruction;
    let instrCount = 0;
    tunnel.oninstruction = function(opcode, params) {
        instrCount++;
        if (instrCount <= 50 || opcode === 'error' || opcode === 'size' || opcode === 'disconnect') {
            console.log('[instr #' + instrCount + ']', opcode, params && params.length > 5 ? '(' + params.length + ' args)' : params);
        }
        if (origOnInstruction) origOnInstruction(opcode, params);
    };

    // State changes
    const STATES = ['IDLE', 'CONNECTING', 'WAITING', 'CONNECTED', 'DISCONNECTING', 'DISCONNECTED'];
    client.onstatechange = function(state) {
        setStatus('State: ' + STATES[state] + ' (#' + instrCount + ' instr)');
    };

    // Connect (empty string - params are injected by the bridge)
    try {
        client.connect('');
    } catch (e) {
        setStatus('Connect failed: ' + e.message, true);
    }

    // --- Mouse input ---
    const mouse = new Guacamole.Mouse(display.getElement());
    mouse.onmousedown = mouse.onmouseup = mouse.onmousemove = function(state) {
        client.sendMouseState(state);
    };

    // --- Keyboard input ---
    const keyboard = new Guacamole.Keyboard(document);
    keyboard.onkeydown = function(keysym) { client.sendKeyEvent(1, keysym); };
    keyboard.onkeyup   = function(keysym) { client.sendKeyEvent(0, keysym); };

    // Disconnect on page close
    window.addEventListener('beforeunload', function() {
        client.disconnect();
    });
}
</script>
</body>
</html>
"""

# ---- Guacamole protocol helpers ----
def guac_encode(*elements):
    """Build Guacamole instruction: length.value,length.value,...;"""
    parts = []
    for el in elements:
        s = str(el)
        parts.append(f"{len(s)}.{s}")
    return (",".join(parts) + ";").encode("utf-8")

def parse_instruction(raw):
    """Parse 'opcode,arg1,arg2;' -> (opcode, [args])"""
    raw = raw.rstrip(";")
    parts = []
    i = 0
    while i < len(raw):
        dot = raw.index(".", i)
        length = int(raw[i:dot])
        value = raw[dot + 1:dot + 1 + length]
        parts.append(value)
        i = dot + 1 + length
        if i < len(raw) and raw[i] == ",":
            i += 1
    return parts[0], parts[1:]

def read_instruction_sync(sock):
    """Read one complete instruction from a blocking TCP socket."""
    buf = b""
    while True:
        chunk = sock.recv(8192)
        if not chunk:
            return None
        buf += chunk
        if b";" in buf:
            idx = buf.index(b";")
            instr = buf[:idx + 1].decode("utf-8", errors="replace")
            leftover = buf[idx + 1:]
            return instr, leftover

def perform_handshake(tcp_sock):
    """Perform select -> args -> connect -> ready handshake on behalf of the
    browser. Returns any leftover bytes the browser should receive first."""
    # Select RDP protocol
    tcp_sock.sendall(guac_encode("select", "rdp"))

    # Read 'args' instruction
    result = read_instruction_sync(tcp_sock)
    if result is None:
        raise RuntimeError("guacd closed connection during select")
    raw, leftover = result
    opcode, args = parse_instruction(raw)
    if opcode != "args":
        raise RuntimeError(f"Expected 'args', got '{opcode}': {args}")

    print(f"[handshake] guacd requested {len(args)} parameters")

    # Standard client announcements the browser would normally send
    tcp_sock.sendall(guac_encode("size", RDP_PARAMS["width"],
                                  RDP_PARAMS["height"], RDP_PARAMS["dpi"]))
    tcp_sock.sendall(guac_encode("audio"))
    tcp_sock.sendall(guac_encode("video"))
    tcp_sock.sendall(guac_encode("image", "image/png", "image/jpeg", "image/webp"))
    tcp_sock.sendall(guac_encode("timezone", "UTC"))

    # Build connect instruction using our RDP_PARAMS
    connect_args = [RDP_PARAMS.get(name, "") for name in args]
    tcp_sock.sendall(guac_encode("connect", *connect_args))

    return leftover

# ---- HTTP server ----
class HTMLHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            body = HTML_CLIENT.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path.startswith("/static/"):
            # Strip /static/ and resolve safely within STATIC_DIR
            rel = self.path[len("/static/"):].split("?", 1)[0]
            target = os.path.normpath(os.path.join(STATIC_DIR, rel))
            if not target.startswith(STATIC_DIR) or not os.path.isfile(target):
                self.send_error(404)
                return
            ctype = "application/javascript" if target.endswith(".js") else "application/octet-stream"
            with open(target, "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "public, max-age=3600")
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_error(404)

    def log_message(self, fmt, *args):
        print(f"[http] {self.address_string()} - {fmt % args}")

def run_http():
    with socketserver.ThreadingTCPServer(("", HTTP_PORT), HTMLHandler) as srv:
        srv.allow_reuse_address = True
        print(f"[http] Serving HTML client on http://localhost:{HTTP_PORT}/")
        srv.serve_forever()

# ---- WebSocket -> TCP bridge ----
async def bridge(websocket):
    print(f"[ws] Browser connected from {websocket.remote_address}")

    # Connect to guacd
    try:
        tcp = socket.create_connection((GUACD_HOST, GUACD_PORT), timeout=10)
        tcp.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except Exception as e:
        print(f"[ws] guacd connect failed: {e}")
        await websocket.close(code=1011, reason=f"guacd unreachable: {e}")
        return

    print(f"[ws] Connected to guacd at {GUACD_HOST}:{GUACD_PORT}")

    # Do the handshake here (browser client will start in "ready" state)
    try:
        leftover = perform_handshake(tcp)
        print("[ws] Handshake complete, entering bridge loop")
    except Exception as e:
        print(f"[ws] Handshake failed: {e}")
        tcp.close()
        await websocket.close(code=1011, reason=str(e))
        return

    loop = asyncio.get_running_loop()
    stop = threading.Event()

    # Hold leftover bytes from handshake - will be processed together with
    # incoming TCP data by the tcp_reader so instruction boundaries are
    # preserved. (Sending a half-complete chunk directly as text crashes
    # the guacamole-common-js parser.)

    # TCP -> WebSocket: run blocking recv in a thread, push to async queue
    tcp_queue = asyncio.Queue()

    def split_complete_instructions(buf):
        """Scan a buffer for complete Guacamole instructions (length-prefixed,
        terminated by ';'). Returns (complete_bytes, remainder) where
        complete_bytes holds zero or more full instructions ready to send and
        remainder is the incomplete trailing data that must stay in buffer.

        Uses the length prefix to find exact instruction boundaries, not just
        ';' (which may appear inside elements as data).
        """
        # Decode as UTF-8, buffering incomplete multibyte sequences
        try:
            text = buf.decode("utf-8")
        except UnicodeDecodeError as e:
            # Keep incomplete UTF-8 bytes for next call
            text = buf[:e.start].decode("utf-8", errors="replace")
            remainder_bytes = buf[e.start:]
        else:
            remainder_bytes = b""

        pos = 0
        last_complete = 0
        n = len(text)
        while pos < n:
            # Parse "length.value" element
            dot = text.find(".", pos)
            if dot == -1:
                break  # incomplete length prefix
            try:
                length = int(text[pos:dot])
            except ValueError:
                # Garbage in stream - take whole text up to here and hope for best
                return text[:pos].encode("utf-8"), text[pos:].encode("utf-8") + remainder_bytes
            elem_start = dot + 1
            elem_end = elem_start + length
            if elem_end >= n:
                break  # incomplete element
            term = text[elem_end]
            pos = elem_end + 1
            if term == ";":
                last_complete = pos  # One full instruction completed here
            elif term != ",":
                # Broken format
                return text[:pos].encode("utf-8"), text[pos:].encode("utf-8") + remainder_bytes
            # If terminator is ',' keep scanning next element

        complete = text[:last_complete].encode("utf-8")
        remainder = text[last_complete:].encode("utf-8") + remainder_bytes
        return complete, remainder

    def tcp_reader():
        """Blocking thread that reads from guacd, buffers until complete
        Guacamole instructions are available, and pushes them to the queue."""
        buf = leftover or b""
        # Drain any complete instructions already present in the leftover
        if buf:
            complete, buf = split_complete_instructions(buf)
            if complete:
                asyncio.run_coroutine_threadsafe(tcp_queue.put(complete), loop)
        try:
            tcp.settimeout(None)  # blocking
            while not stop.is_set():
                try:
                    data = tcp.recv(16384)
                except (ConnectionResetError, OSError) as e:
                    if getattr(e, 'winerror', None) != 10038:
                        print(f"[ws] tcp recv error: {e}")
                    break
                if not data:
                    print("[ws] tcp EOF (guacd closed connection - check guacd log)")
                    break
                buf += data
                complete, buf = split_complete_instructions(buf)
                if complete:
                    asyncio.run_coroutine_threadsafe(tcp_queue.put(complete), loop)
        finally:
            try:
                asyncio.run_coroutine_threadsafe(tcp_queue.put(None), loop)
            except Exception:
                pass
            print("[ws] tcp_reader thread exit")

    reader_thread = threading.Thread(target=tcp_reader, daemon=True)
    reader_thread.start()

    async def tcp_to_ws():
        bytes_fwd = 0
        try:
            while True:
                data = await tcp_queue.get()
                if data is None:
                    break
                await websocket.send(data.decode("utf-8", errors="replace"))
                bytes_fwd += len(data)
        except websockets.ConnectionClosed:
            pass
        except Exception as e:
            print(f"[ws] tcp->ws error: {e}")
        finally:
            print(f"[ws] tcp->ws done, {bytes_fwd} bytes forwarded to browser")

    async def ws_to_tcp():
        bytes_fwd = 0
        try:
            async for msg in websocket:
                if isinstance(msg, str):
                    data = msg.encode("utf-8")
                else:
                    data = msg
                try:
                    # Blocking send - run in executor if large
                    tcp.sendall(data)
                    bytes_fwd += len(data)
                except (ConnectionResetError, OSError) as e:
                    print(f"[ws] tcp send error: {e}")
                    break
        except websockets.ConnectionClosed:
            pass
        except Exception as e:
            print(f"[ws] ws->tcp error: {e}")
        finally:
            print(f"[ws] ws->tcp done, {bytes_fwd} bytes forwarded to guacd")

    try:
        await asyncio.gather(tcp_to_ws(), ws_to_tcp())
    finally:
        stop.set()
        try: tcp.shutdown(socket.SHUT_RDWR)
        except: pass
        try: tcp.close()
        except: pass
        try: await websocket.close()
        except: pass
        print("[ws] Bridge closed")

async def run_ws():
    print(f"[ws] Listening on ws://localhost:{WS_PORT}/")
    async with websockets.serve(bridge, "", WS_PORT, subprotocols=["guacamole"]):
        await asyncio.Future()  # run forever

# ---- Main ----
def main():
    print("=" * 60)
    print("guacd Web Client for Hyper-V RDP")
    print("=" * 60)
    print(f"  guacd target: {GUACD_HOST}:{GUACD_PORT}")
    print(f"  RDP target:   {RDP_PARAMS['hostname']}:{RDP_PARAMS['port']}")
    print(f"  VM GUID:      {RDP_PARAMS['preconnection-blob']}")
    print()

    # Start HTTP server in background thread
    http_thread = threading.Thread(target=run_http, daemon=True)
    http_thread.start()

    # Open browser
    url = f"http://localhost:{HTTP_PORT}/"
    print(f"Opening browser: {url}")
    threading.Timer(1.0, lambda: webbrowser.open(url)).start()

    # Run WebSocket server on main thread (asyncio)
    try:
        asyncio.run(run_ws())
    except KeyboardInterrupt:
        print("\nShutting down.")

if __name__ == "__main__":
    main()
