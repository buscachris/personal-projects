#!/usr/bin/env bash
# GNU binutils m68k build; no external runtime/library is used.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
m68k-elf-as -m68000 -o build/main.o main.s
m68k-elf-ld -T linker.ld -o build/mobius_topology_demo.elf build/main.o
m68k-elf-objcopy -O binary build/mobius_topology_demo.elf build/mobius_topology_demo.bin
python3 validate_rom.py build/mobius_topology_demo.bin
