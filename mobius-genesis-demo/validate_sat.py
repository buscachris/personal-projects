#!/usr/bin/env python3
"""Validate a deterministic VDP/SAT diagnostic ROM and its SAT fixture."""
from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
from pathlib import Path

SAT_VRAM = 0xD800
SPRITE_BUF = 0xFF0800
SPRITE_CAPACITY = 80
VRAM_MAP = {
    "tiles/font": (0x0000, 0x01A0),
    "HScroll": (0xB000, 0xB400),
    "Plane A": (0xC000, 0xD000),
    "SAT": (0xD800, 0xDA80),
    "Plane B": (0xE000, 0xF000),
    "Window": (0xF000, 0x10000),
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"validation failed: {message}")


def symbol_address(elf: Path, name: str) -> int:
    listing = subprocess.check_output(["m68k-elf-nm", "-n", str(elf)], text=True)
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] == name:
            return int(fields[0], 16)
    raise SystemExit(f"validation failed: symbol {name} not found")


def validate_vram_map() -> None:
    ordered = sorted(VRAM_MAP.items(), key=lambda item: item[1][0])
    for name, (start, end) in ordered:
        require(0 <= start < end <= 0x10000, f"invalid {name} range")
    for (left_name, (_, left_end)), (right_name, (right_start, _)) in zip(ordered, ordered[1:]):
        require(left_end <= right_start, f"{left_name} overlaps {right_name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--count", type=int, required=True)
    args = parser.parse_args()
    count = args.count
    require(0 <= count <= SPRITE_CAPACITY, "sprite count must be 0..80")
    require(SPRITE_CAPACITY * 8 == 640, "SAT allocation arithmetic")
    require(SPRITE_BUF + SPRITE_CAPACITY * 8 == 0xFF0A80, "SpriteBuf end")
    command = ((SAT_VRAM & 0x3FFF) << 16) | ((SAT_VRAM & 0xC000) >> 14) | 0x40000000
    require(command == 0x58000003, "SAT command derivation")
    validate_vram_map()

    rom = bytearray(args.rom.read_bytes())
    require(len(rom) >= 0x200 and not len(rom) & 1, "ROM header/even length")
    require(rom[0x100:0x110] == b"SEGA MEGA DRIVE ", "Genesis console header")
    initial_sp, reset = struct.unpack_from(">II", rom, 0)
    vblank = struct.unpack_from(">I", rom, 0x78)[0]
    require(initial_sp == 0x00FFFF00, "initial SP")
    require(0x200 <= reset < len(rom), "reset vector")
    require(0x200 <= vblank < len(rom), "VBlank vector")
    checksum = sum(struct.unpack_from(">H", rom, offset)[0] for offset in range(0x200, len(rom) - 1, 2)) & 0xFFFF
    require(struct.unpack_from(">H", rom, 0x18E)[0] == checksum, "ROM checksum")

    print(f"ROM SHA-1: {hashlib.sha1(rom).hexdigest()}")
    print(f"SAT command: 0x{command:08X} (VRAM write at 0x{SAT_VRAM:04X})")
    print(f"SpriteBuf: 0x{SPRITE_BUF:06X}-0x{SPRITE_BUF + SPRITE_CAPACITY * 8 - 1:06X} ({SPRITE_CAPACITY * 8} bytes)")
    print("VRAM map:")
    for name, (start, end) in VRAM_MAP.items():
        print(f"  {name:10s} 0x{start:04X}-0x{end - 1:04X}")

    if count == 0:
        print("SAT fixture: phase 1 terminator is written directly (4 words, no SpriteBuf upload)")
        return

    start = symbol_address(args.elf, "DiagnosticSAT")
    payload = rom[start:start + count * 8]
    require(len(payload) == count * 8, "SAT fixture within ROM bounds")
    print(f"SAT fixture ({len(payload)} bytes): {payload.hex(' ')}")
    for index in range(count):
        y, size_link, attr, x = struct.unpack_from(">4H", payload, index * 8)
        expected_link = index + 1 if index + 1 < count else 0
        require(96 <= y <= 352, f"sprite {index}: Y out of diagnostic range")
        require(96 <= x <= 448, f"sprite {index}: X out of diagnostic range")
        require((size_link & 0xFF00) == 0, f"sprite {index}: non-1x1 size bits")
        require((size_link & 0xFF) == expected_link, f"sprite {index}: bad link")
        require((attr & 0x07FF) == 1, f"sprite {index}: tile is not uploaded dot tile")
        require((attr & 0xF800) == 0, f"sprite {index}: unexpected palette/priority/flip bits")
        print(f"  {index:02d}: Y={y:3d} size/link=0x{size_link:04X} attr=0x{attr:04X} X={x:3d}")


if __name__ == "__main__":
    main()
