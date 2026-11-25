#!/usr/bin/env bash
# Build one immutable VDP/SAT diagnostic artifact.
set -euo pipefail
cd "$(dirname "$0")"

phase=${1:?usage: ./build_diagnostics.sh phase1|one|four|8|16|32|64|73 output-directory}
out=${2:?usage: ./build_diagnostics.sh phase1|one|four|8|16|32|64|73 output-directory}
case "$phase" in
  phase1) count=0 ;;
  one)    count=1 ;;
  four)   count=4 ;;
  8|16|32|64|73) count=$phase ;;
  *) echo "unknown diagnostic phase: $phase" >&2; exit 2 ;;
esac

mkdir -p "$out"
m68k-elf-as -m68000 --defsym "DIAG_SPRITE_COUNT=$count" -o "$out/vdp_sat_diagnostic.o" vdp_sat_diagnostic.s
m68k-elf-ld -T linker.ld -o "$out/mobius_vdp_sat_${phase}.elf" "$out/vdp_sat_diagnostic.o"
m68k-elf-objcopy -O binary "$out/mobius_vdp_sat_${phase}.elf" "$out/mobius_vdp_sat_${phase}.bin"
python3 validate_rom.py "$out/mobius_vdp_sat_${phase}.bin"
python3 validate_sat.py "$out/mobius_vdp_sat_${phase}.bin" "$out/mobius_vdp_sat_${phase}.elf" --count "$count"
