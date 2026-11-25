#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ASL="${ASL:-asl}"
P2BIN="${P2BIN:-p2bin}"
OUT=${1:-"$ROOT/builds/2026-09-02_01_initial-playable/cyber-shell-v1.bin"}

mkdir -p "$(dirname -- "$OUT")"
cd "$ROOT"
"$ASL" -q -U -E -i "$ROOT" "$ROOT/cyber_shell.asm"
"$P2BIN" "$ROOT/cyber_shell.p" "$OUT"

OUT="$OUT" python3 - <<'PY'
import os
from pathlib import Path

path = Path(os.environ["OUT"])
data = bytearray(path.read_bytes())
if len(data) & 1:
    data.append(0)

# Standard Genesis header fields: ROM end at $1A4, checksum at $18E.
data[0x1A0:0x1A4] = (0).to_bytes(4, "big")
data[0x1A4:0x1A8] = (len(data) - 1).to_bytes(4, "big")
data[0x18E:0x190] = b"\0\0"
checksum = sum(int.from_bytes(data[i:i+2], "big") for i in range(0x200, len(data), 2)) & 0xFFFF
data[0x18E:0x190] = checksum.to_bytes(2, "big")
path.write_bytes(data)
print(f"built {path} ({len(data)} bytes) checksum=0x{checksum:04X}")
PY

sha1sum "$OUT"
