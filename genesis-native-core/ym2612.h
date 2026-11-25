/*
 * ym2612.h - YM2612 FM sound chip (OPN2) for Sega Genesis/Mega Drive
 *
 * The YM2612 (aka OPN2) is a 6-channel FM synthesizer.  Each channel
 * has 4 operators (M1, C1, M2, C2) combined in one of 8 algorithms.
 * Channels 5 and 6 can special-case into DAC mode.
 *
 * This module implements register decode and write only — no FM
 * synthesis output yet.  The host can poll channel state for a
 * separate phase-accumulation / envelope engine.
 *
 * Register layout (two banks of 256 bytes each):
 *   Bank 0 → channels 1-3   (write part1 address at $4000, data at $4001)
 *   Bank 1 → channels 4-6   (write part2 address at $4002, data at $4003)
 *
 * Copyright (c) 2026 SoniCkity/Devkity project
 */

#ifndef DK_YM2612_H
#define DK_YM2612_H

#include "dk_types.h"

/* ---------- Channel / operator constants ------------------------------- */

#define DK_YM_CHANNELS     6
#define DK_YM_OPS_PER_CH  4
#define DK_YM_BANKS       2
#define DK_YM_REGS        256

/* ---------- YM2612 state structure ------------------------------------- */

typedef struct dk_ym2612_s {
    /* Two 256-byte register arrays (bank 0 = ch1-3, bank 1 = ch4-6) */
    dk_u8 regs[DK_YM_BANKS][DK_YM_REGS];

    /* Address latch: the Z80 writes the register address first, then
     * the data byte.  We store the latched address per bank. */
    dk_u8 latch[DK_YM_BANKS];

    /* Key-on/off state per channel (bit 0-5 = ch1-ch6) */
    dk_u8 key_on[DK_YM_CHANNELS];

    /* DAC mode (channel 6) */
    dk_u8 dac_data;     /* $2A – raw DAC output byte */
    dk_u8 dac_enable;   /* $2B – 1 = DAC enabled, 0 = FM on ch6 */
    dk_u8 lfo_enable;   /* $22 – 1 = LFO active */

    /* Timers (for $27 Timer Control) */
    int timer_a;        /* Timer A preset (10-bit) */
    int timer_b;        /* Timer B preset (8-bit)  */
    int timer_a_cnt;    /* Running counter A */
    int timer_b_cnt;    /* Running counter B */
    dk_u8 timer_ctrl;   /* Raw $27 value for load/enable/irq flags */

    /* Busy flag timing (approximate — YM2612 has a ~32-cycle busy
     * window after a data write; we just decrement) */
    int busy_cycles;

    /* Cumulative cycle count (for timer tick) */
    dk_u32 cycles;
} dk_ym2612_t;

/* ---------- Public API ------------------------------------------------- */

/* Power-on init */
void dk_ym2612_init(dk_ym2612_t *ym);

/* Soft reset (preserves register contents, clears key-on) */
void dk_ym2612_reset(dk_ym2612_t *ym);

/*
 * Write to the YM2612.
 *   bank = 0 for part 1 (channels 1-3)
 *   bank = 1 for part 2 (channels 4-6)
 *   data = byte written to the data port
 *
 * The address *must* have been latched previously via dk_ym2612_write_addr.
 */
void dk_ym2612_write(dk_ym2612_t *ym, int bank, dk_u8 data);

/*
 * Latch the register address for a subsequent data write.
 *   bank = 0 or 1
 *   addr = register number ($00-$FF)
 */
void dk_ym2612_write_addr(dk_ym2612_t *ym, int bank, dk_u8 addr);

/*
 * Read data port (used by Z80 IN from YM2612).
 * Returns 0 for most registers; status reads return the busy/irq flags.
 */
dk_u8 dk_ym2612_read(dk_ym2612_t *ym, int bank);

/*
 * Read status byte (port $4000 / $4002):
 *   bit 7 = BUSY (1 if chip is processing a write)
 *   bit 0 = Timer A overflow flag
 *   bit 1 = Timer B overflow flag
 */
dk_u8 dk_ym2612_read_status(dk_ym2612_t *ym);

/*
 * Advance timers by the given number of YM2612 clock cycles.
 * The YM2612 runs at the Z80 clock rate (MCLK/15 ≈ 3.58 MHz).
 */
void dk_ym2612_update(dk_ym2612_t *ym, int cycles);

#endif /* DK_YM2612_H */
