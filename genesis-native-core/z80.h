/*
 * z80.h - Z80 sub-CPU for Sega Genesis/Mega Drive
 *
 * The Z80 runs at ~3.58 MHz (MCLK/15) in the Genesis and serves as the
 * sound sub-system CPU, driving the YM2612 FM synthesizer and the PSG.
 * It has 8KB of local RAM at $0000-$1FFF and accesses the 68k bus
 * through bank-switching and request/acknowledge hardware.
 *
 * This implementation targets the SMPS (Sega Mega Drive Sound Driver)
 * music driver — the most common Z80 program in Genesis games.  All
 * opcodes that SMPS uses are included; exotic instructions (e.g. block
 * I/O, RLD/RRD) are NOP'd with a cycle penalty.
 *
 * Copyright (c) 2026 SoniCkity/Devkity project
 */

#ifndef DK_Z80_H
#define DK_Z80_H

#include "dk_types.h"

/* ---------- Z80 state structure ---------------------------------------- */

typedef struct dk_z80_s {
    /* Main register set (AF split so flags are easy to test) */
    dk_u8 af_hi;        /* A  – accumulator */
    dk_u8 flags;        /* F  – SZ5H3PNC flags */

    dk_u8 b, c;         /* BC */
    dk_u8 d, e;         /* DE */
    dk_u8 h, l;         /* HL */

    /* Alternate register set (EX AF,AF' / EXX) */
    dk_u8 af_hi_alt;
    dk_u8 flags_alt;
    dk_u8 b_alt, c_alt;
    dk_u8 d_alt, e_alt;
    dk_u8 h_alt, l_alt;

    /* Index registers */
    dk_u16 ix;           /* IX  (DD prefix) */
    dk_u16 iy;           /* IY  (FD prefix) */

    /* Stack pointer & program counter */
    dk_u16 sp;
    dk_u16 pc;

    /* Interrupt / refresh */
    dk_u8 iff1;          /* Interrupt Flip-Flop 1 (EI/DI) */
    dk_u8 iff2;          /* Interrupt Flip-Flop 2 (for NMI) */
    dk_u8 im;            /* Interrupt mode: 0, 1, or 2 */
    dk_u8 i_reg;         /* I  – interrupt vector base */
    dk_u8 r_reg;         /* R  – refresh counter (7-bit) */

    /* 8 KB local RAM ($0000-$1FFF) */
    dk_u8 ram[0x2000];

    /* Genesis-specific bus control */
    dk_u8 rom_bank;      /* Bank register at $FFFF — selects 32KB window */
    dk_u8 busreq;        /* 68k bus request ($A11100): 0=Z80 has bus */
    dk_u8 reset_flag;    /* Z80 reset ($A11200): 0=active (held in reset) */

    /* YM2612 DAC passthrough flag */
    dk_u8 ym_dac_enable;

    /* CPU state */
    dk_u8 halted;        /* 1 when HALT instruction executed */
    dk_u32 cycles;       /* Total cycles executed (for sync) */

    /* --- Callback context for 68k bus accesses --- */
    /* When the Z80 reads/writes outside its local RAM (e.g. VDP port
     * $7F00 or banked ROM), we need a hook into the 68k bus.  The
     * host emulator sets these after init. */
    dk_u8 (*read_68k)(void *ctx, dk_u32 addr);
    void (*write_68k)(void *ctx, dk_u32 addr, dk_u8 val);
    void *bus_ctx;       /* Opaque pointer passed to callbacks */

    /* Pointer to attached YM2612 (set by host) */
    struct dk_ym2612_s *ym;
} dk_z80_t;

/* ---------- Public API ------------------------------------------------- */

/* Initialise Z80 to power-on state (all regs 0, RAM zeroed, PC=0) */
void dk_z80_init(dk_z80_t *z80);

/* Reset Z80 (as if hardware RESET asserted then released) */
void dk_z80_reset(dk_z80_t *z80);

/* 68k writes the bus-request register ($A11100) */
void dk_z80_set_busreq(dk_z80_t *z80, dk_u8 value);

/* 68k writes the Z80-reset register ($A11200) */
void dk_z80_set_reset(dk_z80_t *z80, dk_u8 value);

/* 68k reads a byte from Z80 address space (for 68k→Z80 RAM peek) */
dk_u8 dk_z80_read_bus(dk_z80_t *z80, dk_u16 addr);

/* 68k writes a byte into Z80 address space (for 68k→Z80 RAM poke) */
void dk_z80_write_bus(dk_z80_t *z80, dk_u16 addr, dk_u8 value);

/*
 * Execute Z80 instructions until at least max_cycles Z80 cycles have
 * been consumed.  Returns the actual number of cycles executed.
 *
 * If the Z80 is held in reset or the 68k owns the bus, this returns 0.
 * Interrupts are checked before each instruction.
 */
int dk_z80_execute(dk_z80_t *z80, int max_cycles);

#endif /* DK_Z80_H */
