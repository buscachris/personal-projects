# Cyber Shell

Cyber Shell is a Sega Genesis / Mega Drive homebrew prototype built as a
parody cyber-dungeon. It combines NetHack-style turn-by-turn grid play with a
small original orbital-AI story and 68000-flavoured assembly hacking puzzles.

The current vertical slice includes:

- three selectable difficulty modes with different HP, energy, and puzzle
  behavior;
- a 20x12 dungeon map with walls, a locked access door, an access chip,
  terminal, exit, and sentry;
- cardinal movement that consumes turns, wait-to-recharge, adjacent shock
  combat, inventory state, death, and victory;
- an interactive terminal puzzle where the player chooses an assembly patch
  with the D-pad, applies it with A, or aborts with C.

This is an original clean-room prototype. It does not include proprietary
System Shock assets or dialogue.

## Build

Install Macro Assembler AS and its p2bin utility or set `ASL` and `P2BIN` to their paths. This edition uses an original compact display font.

```sh
./build.sh
```

The default output is:

`builds/2026-09-02_01_initial-playable/cyber-shell-v1.bin`

Run it in a Genesis emulator such as BlastEm:

```sh
blastem builds/2026-09-02_01_initial-playable/cyber-shell-v1.bin
```
