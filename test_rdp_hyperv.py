#!/usr/bin/env python3
"""
Test script for guacd.exe RDP connection to Hyper-V VM.

Connects to guacd via the Guacamole protocol, initiates an RDP session
to a Hyper-V VM using the preconnection blob (VM GUID), and dumps
the protocol exchange to verify the connection works.

Usage:
    1. Start guacd:  guacd.exe -f -L debug -b 127.0.0.1 -l 4822
    2. Run this:      python test_rdp_hyperv.py
"""

import socket
import sys
import time
import threading

# ---- Configuration ----
GUACD_HOST = "localhost"
GUACD_PORT = 4822

# Hyper-V VM RDP settings (from .rdp file)
RDP_HOSTNAME = "localhost"
RDP_PORT = "2179"
RDP_WIDTH = "1600"
RDP_HEIGHT = "900"
RDP_DPI = "96"
RDP_COLOR_DEPTH = "32"
RDP_SECURITY = "vmconnect"     # Hyper-V VMConnect-specific mode
RDP_IGNORE_CERT = "true"       # Skip cert validation for local VM
RDP_PRECONNECTION_BLOB = "9defd48b-957e-4b9f-9fba-eb620d2b6c86"  # VM GUID
RDP_DISABLE_AUTH = "false"     # Hyper-V uses current Windows user (NTLM/Kerberos)


def guac_encode(*elements):
    """Encode a Guacamole protocol instruction.

    Format: length.value,length.value,...;
    Example: 6.select,3.rdp;
    """
    parts = []
    for el in elements:
        s = str(el)
        parts.append(f"{len(s)}.{s}")
    return (",".join(parts) + ";").encode("utf-8")


def guac_read_instruction(sock):
    """Read one complete Guacamole instruction (terminated by ';')."""
    buf = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            return None
        buf += chunk
        if b";" in buf:
            # May contain multiple instructions; return first
            idx = buf.index(b";")
            instruction = buf[:idx + 1].decode("utf-8", errors="replace")
            return instruction


def guac_parse_instruction(raw):
    """Parse a Guacamole instruction string into (opcode, [args])."""
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


def main():
    print("=" * 60)
    print("guacd RDP/Hyper-V Connection Test")
    print("=" * 60)
    print(f"  guacd:    {GUACD_HOST}:{GUACD_PORT}")
    print(f"  RDP:      {RDP_HOSTNAME}:{RDP_PORT}")
    print(f"  VM GUID:  {RDP_PRECONNECTION_BLOB}")
    print(f"  Display:  {RDP_WIDTH}x{RDP_HEIGHT} @ {RDP_COLOR_DEPTH}bpp")
    print()

    # Connect to guacd
    print("[1] Connecting to guacd...")
    try:
        sock = socket.create_connection((GUACD_HOST, GUACD_PORT), timeout=10)
    except ConnectionRefusedError:
        print("    ERROR: Cannot connect to guacd. Is it running?")
        print(f"    Start it with: guacd.exe -f -L debug -b {GUACD_HOST} -l {GUACD_PORT}")
        sys.exit(1)
    print("    Connected!")

    # Step 1: Send "select" instruction to choose RDP protocol
    print("[2] Sending protocol selection (rdp)...")
    sock.sendall(guac_encode("select", "rdp"))

    # Step 2: Read "args" instruction - guacd tells us what parameters it expects
    print("[3] Waiting for parameter list from guacd...")
    response = guac_read_instruction(sock)
    if response is None:
        print("    ERROR: No response from guacd. Plugin may not be loaded.")
        print("    Ensure guac-client-rdp.dll is in the same directory as guacd.exe")
        sock.close()
        sys.exit(1)

    opcode, args = guac_parse_instruction(response)
    print(f"    Received: {opcode} with {len(args)} parameters")

    if opcode != "args":
        print(f"    ERROR: Expected 'args', got '{opcode}'")
        if opcode == "error":
            print(f"    Server error: {args}")
        sock.close()
        sys.exit(1)

    # Step 3: Build parameter values matching the args list
    # Map known parameter names to our values
    param_values = {
        "hostname":             RDP_HOSTNAME,
        "port":                 RDP_PORT,
        "width":                RDP_WIDTH,
        "height":               RDP_HEIGHT,
        "dpi":                  RDP_DPI,
        "color-depth":          RDP_COLOR_DEPTH,
        "security":             RDP_SECURITY,
        "ignore-cert":          RDP_IGNORE_CERT,
        "disable-auth":         RDP_DISABLE_AUTH,
        "preconnection-blob":   RDP_PRECONNECTION_BLOB,
        "resize-method":        "display-update",
        "enable-font-smoothing": "true",
        "enable-wallpaper":     "false",
        "enable-theming":       "true",
        "enable-drive":         "false",
        "create-drive-path":    "false",
        "enable-audio":         "false",
        "enable-printing":      "false",
        "console":              "false",
    }

    # Build connect args in the order guacd expects
    connect_args = []
    for param_name in args:
        value = param_values.get(param_name, "")
        connect_args.append(value)

    # Print parameter mapping for debugging
    print("\n    Parameter mapping:")
    for i, name in enumerate(args):
        val = connect_args[i]
        if val:
            print(f"      {name} = {val}")

    # Step 4a: Client handshake (size/audio/video/image) BEFORE connect.
    # guacd uses these to configure user->info.optimal_width/height which
    # the RDP plugin reads for its actual resolution calculation.
    print("\n[4] Sending client handshake (size/audio/video/image/timezone)...")
    sock.sendall(guac_encode("size", RDP_WIDTH, RDP_HEIGHT, RDP_DPI))
    sock.sendall(guac_encode("audio", "audio/L8", "audio/L16"))
    sock.sendall(guac_encode("video"))
    sock.sendall(guac_encode("image", "image/png", "image/jpeg", "image/webp"))
    sock.sendall(guac_encode("timezone", "UTC"))

    # Step 4b: Send "connect" instruction with all parameters
    print(f"    Sending connect instruction ({len(connect_args)} params)...")
    sock.sendall(guac_encode("connect", *connect_args))

    # Step 5: Read response - should be "ready" with connection ID
    print("[5] Waiting for connection response...")
    response = guac_read_instruction(sock)
    if response is None:
        print("    ERROR: No response after connect")
        sock.close()
        sys.exit(1)

    opcode, args = guac_parse_instruction(response)
    print(f"    Received: {opcode} {args}")

    if opcode == "ready":
        conn_id = args[0] if args else "unknown"
        print(f"\n    SUCCESS! Connection established.")
        print(f"    Connection ID: {conn_id}")
    elif opcode == "error":
        print(f"\n    Connection failed: {args}")
        sock.close()
        sys.exit(1)
    else:
        print(f"\n    Unexpected response: {opcode}")

    # Step 6: Read further protocol traffic - graphics, errors, or disconnect
    print("\n[6] Reading RDP protocol stream...")
    sock.settimeout(8)
    instruction_count = 0
    got_error = None
    try:
        buf = b""
        while instruction_count < 30:
            chunk = sock.recv(8192)
            if not chunk:
                print("    Connection closed by server (RDP target not reachable)")
                break
            buf += chunk
            while b";" in buf:
                idx = buf.index(b";")
                raw = buf[:idx + 1].decode("utf-8", errors="replace")
                buf = buf[idx + 1:]
                op, a = guac_parse_instruction(raw)
                instruction_count += 1
                # Show first few instructions
                if instruction_count <= 15:
                    if op in ("img", "png", "copy", "rect", "cfill"):
                        print(f"    [{instruction_count}] {op} (graphics data)")
                    elif op == "size":
                        print(f"    [{instruction_count}] {op}: layer={a[0]} w={a[1]} h={a[2]}")
                    elif op == "name":
                        print(f"    [{instruction_count}] {op}: {a[0]}")
                    elif op == "error":
                        got_error = a
                        print(f"    [{instruction_count}] ERROR: {a}")
                    elif op == "disconnect":
                        print(f"    [{instruction_count}] disconnect from server")
                    else:
                        summary = str(a)[:80]
                        print(f"    [{instruction_count}] {op}: {summary}")
    except socket.timeout:
        print("    (timeout waiting for more data)")
    except ConnectionResetError:
        print("    Connection reset by server (expected if target VM not running)")

    print()
    if instruction_count > 0:
        print(f"    Received {instruction_count} Guacamole instructions")
    if got_error:
        print(f"    RDP plugin reported error: {got_error}")
        print("    This is expected if the Hyper-V VM with the specified")
        print("    preconnection-blob GUID is not currently running.")
    elif instruction_count > 3:
        print("    Connection is LIVE - RDP session active!")

    # Cleanup
    print("\n[7] Sending disconnect...")
    try:
        sock.sendall(guac_encode("disconnect"))
    except:
        pass
    sock.close()

    print("\n" + "=" * 60)
    print("Test complete!")
    print("=" * 60)


if __name__ == "__main__":
    main()
