#!/usr/bin/env python3
"""Send the Overhead Status Badge parameter to VRChat over OSC.

This intentionally uses only the Python standard library so it works on a fresh
machine without installing python-osc.
"""

import argparse
import socket
import struct
import sys


STATUSES = {
    "hidden": 0,
    "available": 1,
    "busy": 2,
    "afk": 3,
    "recording": 4,
    "coding": 5,
}


def osc_string(value: str) -> bytes:
    encoded = value.encode("utf-8") + b"\0"
    padding = (4 - (len(encoded) % 4)) % 4
    return encoded + (b"\0" * padding)


def osc_int(value: int) -> bytes:
    return struct.pack(">i", value)


def send_status(host: str, port: int, value: int) -> None:
    packet = (
        osc_string("/avatar/parameters/StatusMode")
        + osc_string(",i")
        + osc_int(value)
    )
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(packet, (host, port))


def main() -> int:
    parser = argparse.ArgumentParser(description="Set VRChat avatar StatusMode over OSC.")
    parser.add_argument("status", choices=sorted(STATUSES), help="Status to display.")
    parser.add_argument("--host", default="127.0.0.1", help="VRChat OSC host.")
    parser.add_argument("--port", type=int, default=9000, help="VRChat OSC input port.")
    args = parser.parse_args()

    value = STATUSES[args.status]
    send_status(args.host, args.port, value)
    print(f"sent StatusMode={value} ({args.status})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
