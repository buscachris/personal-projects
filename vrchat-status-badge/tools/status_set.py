#!/usr/bin/env python3
"""Set the status used by status_live.py and send it once immediately."""

import argparse
from pathlib import Path
import sys

from status_live import DEFAULT_STATE_FILE
from status_osc import STATUSES, send_status


def main() -> int:
    parser = argparse.ArgumentParser(description="Set VRChat overhead status.")
    parser.add_argument("status", choices=sorted(STATUSES), help="Status to display.")
    parser.add_argument("--state-file", default=str(DEFAULT_STATE_FILE), help="Status file for status_live.py.")
    parser.add_argument("--host", default="127.0.0.1", help="VRChat OSC host.")
    parser.add_argument("--port", type=int, default=9000, help="VRChat OSC input port.")
    args = parser.parse_args()

    state_file = Path(args.state_file).expanduser()
    state_file.parent.mkdir(parents=True, exist_ok=True)
    state_file.write_text(args.status + "\n", encoding="utf-8")

    value = STATUSES[args.status]
    send_status(args.host, args.port, value)
    print(f"set StatusMode={value} ({args.status})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

