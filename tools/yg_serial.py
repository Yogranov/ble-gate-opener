#!/usr/bin/env python3
"""Talk to the unit's USB console without an interactive terminal.

    python tools/yg_serial.py status                  run one command
    python tools/yg_serial.py --listen 5              print output for 5 s
    python tools/yg_serial.py --reset --listen 6      restart it, then print
"""
import argparse
import sys
import time

import serial

PROMPT = b"yg>"


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.1
    # Keep both low so that opening the port does not reset the chip.
    s.dtr = False
    s.rts = False
    s.open()
    return s


def reset(s):
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False


def read_for(s, seconds, stop=None):
    out = bytearray()
    end = time.time() + seconds
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            out += chunk
            end = max(end, time.time() + 0.3) if stop is None else end
            if stop is not None and stop in out:
                break
    return bytes(out)


def run_command(s, command, timeout):
    """Sends one command and returns what it printed. The console echoes each
    command, so the answer is everything between that echo and the next prompt."""
    s.reset_input_buffer()
    encoded = command.encode()
    s.write(encoded + b"\n")
    s.flush()
    raw = bytearray()
    start = -1
    prompt = -1
    end = time.time() + timeout
    while time.time() < end and prompt < 0:
        raw += s.read(4096)
        if start < 0:
            found = raw.find(encoded)
            if found >= 0:
                start = found + len(encoded)
        if start >= 0:
            prompt = raw.find(PROMPT, start)
    stop = prompt if prompt >= 0 else len(raw)
    text = bytes(raw[max(start, 0):stop]).decode(errors="replace").replace("\r", "")
    return text.strip("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--listen", type=float)
    parser.add_argument("command", nargs="*")
    args = parser.parse_args()

    s = open_port(args.port)
    if args.reset:
        reset(s)
    if args.listen:
        sys.stdout.write(read_for(s, args.listen).decode(errors="replace"))
    if args.command:
        print(run_command(s, " ".join(args.command), args.timeout))
    s.close()


if __name__ == "__main__":
    main()
