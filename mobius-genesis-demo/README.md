# Möbius Topology Demo for Sega Genesis / Mega Drive

A self-contained Motorola 68000 ROM, built without SGDK, external libraries,
or a parent game ROM. It is deliberately a first hardware-safe renderer: a
32-sample, sprite-based wireframe ribbon, rather than a framebuffer or a
software polygon renderer.

## Build

Requirements: GNU `m68k-elf-as`, `m68k-elf-ld`, `m68k-elf-objcopy`, and
Python 3. Run:

```sh
./build.sh
```

Expected ROM: `build/mobius_topology_demo.bin`.

## VDP/SAT diagnostic sequence

`vdp_sat_diagnostic.s` is a separate, fixed diagnostic harness. It has no
Möbius geometry, projection, controller processing, dynamic text, or runtime
VDP writes. It uses this non-overlapping VRAM map:

| Resource | Occupied range |
| --- | --- |
| Font tiles | `$0000-$019F` |
| HScroll | `$B000-$B3FF` |
| Plane A | `$C000-$CFFF` |
| SAT (80 entries reserved) | `$D800-$DA7F` |
| Plane B | `$E000-$EFFF` |
| Window | `$F000-$FFFF` |

Build each phase into a new timestamped directory, for example:

```sh
./build_diagnostics.sh phase1 builds/2026-09-24_vdp-sat-phase1
./build_diagnostics.sh one    builds/2026-09-24_vdp-sat-one
./build_diagnostics.sh four   builds/2026-09-24_vdp-sat-four
./build_diagnostics.sh 8      builds/2026-09-24_vdp-sat-8
```

Then continue with `16`, `32`, `64`, and `73`. `validate_sat.py` validates the
ROM vectors/checksum/bounds, the `$D800 -> $58000003` SAT write command,
SpriteBuf capacity, all map ranges, and every generated SAT fixture record.

## Controls

- Left / Right: rotate around Y
- Up / Down: rotate around X
- A: switch NORMAL RING / MOBIUS STRIP
- B: toggle automatic rotation
- C: toggle tracer animation
- Hold C + Left / Right: decrease / increase twist (0, 180, or 360 shown)
- Start: restore the opening Möbius state

The tracer uses a 512-step path. In Möbius mode it changes its offset after
the first 256-step lap, then returns to its original local orientation after
the second. The status line identifies the current first or second lap.

## Rendering notes

The strip is a real fixed-point parametric ribbon: `u` uses a 256-entry
angular space and `v` is the signed strip half-width. Sine/cosine values come
from a quarter-wave lookup table; object rotation and perspective projection
are integer-only. It is intentionally a wireframe milestone, not a filled
polygon renderer.
