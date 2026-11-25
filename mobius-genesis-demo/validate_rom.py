#!/usr/bin/env python3
"""Stamp and sanity-check the standalone Mega Drive ROM."""
from pathlib import Path
import hashlib, struct, sys

p = Path(sys.argv[1])
d = bytearray(p.read_bytes())
if len(d) < 0x200:
    raise SystemExit("ROM is shorter than a Genesis header")
if len(d) & 1:
    raise SystemExit("ROM must have an even byte length")
checksum = sum(struct.unpack_from(">H", d, n)[0] for n in range(0x200, len(d)-1, 2)) & 0xffff
struct.pack_into(">H", d, 0x18e, checksum)
p.write_bytes(d)
assert d[0x100:0x110] == b"SEGA MEGA DRIVE "
assert struct.unpack_from(">I", d, 0)[0] == 0x00ffff00
reset = struct.unpack_from(">I", d, 4)[0]
assert 0x200 <= reset < len(d)
vblank = struct.unpack_from(">I", d, 0x78)[0]
assert 0x200 <= vblank < len(d)
assert struct.unpack_from(">I", d, 0x1a0)[0] == 0
assert struct.unpack_from(">I", d, 0x1a4)[0] == len(d) - 1
assert struct.unpack_from(">H", d, 0x18e)[0] == checksum
print("ROM:", p.resolve())
print("size:", len(d), "bytes")
print("sha1:", hashlib.sha1(d).hexdigest())
print("checksum: 0x%04X" % checksum)
