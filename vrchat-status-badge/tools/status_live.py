#!/usr/bin/env python3
"""Keep a VRChat overhead status parameter live over OSC.

Run this while VRChat is open. It repeatedly sends the current status so avatar
swaps pick it up as soon as the newly loaded avatar has the StatusMode
parameter.
"""

import argparse
from pathlib import Path
import signal
import sys
import time

from status_osc import STATUSES, send_status


DEFAULT_STATE_FILE = Path.home() / ".config" / "vrchat_status_badge" / "status.txt"


def read_status(path: Path, fallback: str) -> str:
    try:
        value = path.read_text(encoding="utf-8").strip().lower()
    except FileNotFoundError:
        return fallback

    if value in STATUSES:
        return value

    return fallback


def write_status(path: Path, status: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(status + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Continuously send VRChat StatusMode over OSC.")
    parser.add_argument("status", nargs="?", choices=sorted(STATUSES), help="Initial status.")
    parser.add_argument("--state-file", default=str(DEFAULT_STATE_FILE), help="Status file to watch.")
    parser.add_argument("--host", default="127.0.0.1", help="VRChat OSC host.")
    parser.add_argument("--port", type=int, default=9000, help="VRChat OSC input port.")
    parser.add_argument("--interval", type=float, default=1.0, help="Seconds between sends.")
    args = parser.parse_args()

    state_file = Path(args.state_file).expanduser()
    status = args.status or read_status(state_file, "hidden")
    write_status(state_file, status)

    running = True

    def stop(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    print(f"live status sender running: {state_file}")
    print("change status by editing that file or using status_set.py")

    last_status = None
    while running:
        status = read_status(state_file, status)
        value = STATUSES[status]
        send_status(args.host, args.port, value)

        if status != last_status:
            print(f"StatusMode={value} ({status})")
            last_status = status

        time.sleep(max(args.interval, 0.1))

    print("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())

