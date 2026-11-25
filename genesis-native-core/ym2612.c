/*
 * ym2612.c - YM2612 FM sound chip (OPN2) register decode
 *
 * This file implements register-level access to the YM2612 as used by
 * the Genesis Z80 sub-CPU.  No FM synthesis output is produced yet;
 * the host can inspect the register arrays to drive a separate
 * phase-accumulation / envelope engine.
 *
 * Register summary:
 *   $22  LFO enable
 *   $24  Timer A MSB (bits 9-2)
 *   $25  Timer A LSB (bits 1-0) / Timer B MSB
 *   $26  Timer B LSB
 *   $27  Timer control (load/enable/irq reset/ch3 mode)
 *   $28  Key on/off
 *   $29  Unused (SoniCkity: NOP)
 *   $2A  DAC data
 *   $2B  DAC enable
 *   $30-$3F  DT1/MUL per operator (4 ops × 3 ch per bank)
 *   $40-$4F  TL   (total level)
 *   $50-$5F  KS/AR (key scale / attack rate)
 *   $60-$6F  AM/DR (AM enable / decay rate)
 *   $70-$7F  SR   (sustain rate)
 *   $80-$8F  SL/RR (sustain level / release rate)
 *   $90-$9F  SSG-EG
 *   $A0-$A3  Frequency LSB  (ch1-3 / ch4-6)
 *   $A4-$A7  Block + Freq MSB
 *   $A8-$AB  Ch3 supplemental freq LSB  (bank 0 only)
 *   $AC-$AF  Ch3 supplemental block+freq MSB (bank 0 only)
 *   $B0-$B3  Feedback / Algorithm
 *   $B4-$B7  Pan / AMS / FMS
 *
 * Copyright (c) 2026 SoniCkity/Devkity project
 */

#include "ym2612.h"
#include <string.h>

/* ---------- Internal helpers ------------------------------------------- */

/* Update key-on/off state from register $28 write.
 * $28 format: bit7=0, bit6-4=channel(0-5 or 2 special), bit3=0, bit2-0=key */
static void dk_ym_key_on_off(dk_ym2612_t *ym, dk_u8 data)
{
    int ch;
    dk_u8 key_bits;

    /* Bits 6-4 select the channel (0-5).
     * Note: hardware channel numbering: 0=ch1, 1=ch2, 2=ch3,
     *                                4=ch4, 5=ch5, 6=ch6.
     * Value 3 is invalid on real hardware. */
    ch = (data >> 4) & 0x07u;
    if (ch == 3) return;  /* Invalid slot — real YM2612 ignores this */

    /* Map 0-2 → ch0-2, 4-6 → ch3-5 */
    if (ch >= 4) ch--;

    key_bits = data & 0x07u;  /* bits 2-0: operator key on/off */

    /* Store 6-bit key-on mask for the channel */
    if (ch >= 0 && ch < DK_YM_CHANNELS) {
        ym->key_on[ch] = key_bits;
    }
}

/* Update timer A preset from $24/$25 */
static void dk_ym_update_timer_a(dk_ym2612_t *ym)
{
    /* Timer A = 10-bit value: $24[7:2] << 2 | $25[9:8] */
    dk_u8 msb = ym->regs[0][0x24];
    dk_u8 lsb = ym->regs[0][0x25];
    ym->timer_a = (int)(((dk_u16)(msb & 0xFFu) << 2) | ((lsb >> 6) & 0x03u));
}

/* Update timer B preset from $25/$26 */
static void dk_ym_update_timer_b(dk_ym2612_t *ym)
{
    /* Timer B = 8-bit value: $25[7:0] is upper, $26[7:2] is lower */
    dk_u8 val = ym->regs[0][0x26];
    ym->timer_b = (int)(val & 0xFFu);
}

/* ---------- Public API ------------------------------------------------- */

void dk_ym2612_init(dk_ym2612_t *ym)
{
    if (ym == 0) return;
    memset(ym, 0, sizeof(*ym));
}

void dk_ym2612_reset(dk_ym2612_t *ym)
{
    int i;
    if (ym == 0) return;

    /* Clear key-on state */
    for (i = 0; i < DK_YM_CHANNELS; i++) {
        ym->key_on[i] = 0;
    }

    /* Clear latches */
    ym->latch[0] = 0;
    ym->latch[1] = 0;

    /* Reset timer counters */
    ym->timer_a_cnt = 0;
    ym->timer_b_cnt = 0;
    ym->busy_cycles = 0;

    /* Note: we do NOT clear the register arrays on soft reset.
     * Real YM2612 retains register values across reset; only
     * key-on is forced off. */
}

void dk_ym2612_write_addr(dk_ym2612_t *ym, int bank, dk_u8 addr)
{
    if (ym == 0) return;
    if (bank < 0 || bank >= DK_YM_BANKS) return;

    /* Latch the register address for the next data write */
    ym->latch[bank] = addr;
}

void dk_ym2612_write(dk_ym2612_t *ym, int bank, dk_u8 data)
{
    dk_u8 reg;
    if (ym == 0) return;
    if (bank < 0 || bank >= DK_YM_BANKS) return;

    /* A data-port write asserts BUSY for an approximate 32 YM cycles. */
    ym->busy_cycles = 32;

    reg = ym->latch[bank];

    /* --- Shared registers (bank 0 only) -------------------------------- */

    if (bank == 0) {
        switch (reg) {
            /* $22: LFO enable (bit 3 = LFO on/off, bits 2-0 = LFO freq) */
            case 0x22:
                ym->lfo_enable = (data >> 3) & 1;
                ym->regs[0][0x22] = data;
                return;

            /* $24: Timer A MSB */
            case 0x24:
                ym->regs[0][0x24] = data;
                dk_ym_update_timer_a(ym);
                return;

            /* $25: Timer A LSB + Timer B high bits */
            case 0x25:
                ym->regs[0][0x25] = data;
                dk_ym_update_timer_a(ym);
                return;

            /* $26: Timer B data */
            case 0x26:
                ym->regs[0][0x26] = data;
                dk_ym_update_timer_b(ym);
                return;

            /* $27: Timer control
             *   bit 7 = Timer A load (1 = reload preset)
             *   bit 6 = Timer A enable
             *   bit 5 = Timer A irq clear
             *   bit 4 = Timer B load
             *   bit 3 = Timer B enable
             *   bit 2 = Timer B irq clear
             *   bit 1-0 = Channel 3 mode (0=normal, 1-3=special) */
            case 0x27:
                ym->timer_ctrl = data;
                if (data & 0x80u) ym->timer_a_cnt = ym->timer_a;
                if (data & 0x10u) ym->timer_b_cnt = ym->timer_b;
                ym->regs[0][0x27] = data;
                return;

            /* $28: Key on/off (shared — channel select in data itself) */
            case 0x28:
                dk_ym_key_on_off(ym, data);
                ym->regs[0][0x28] = data;
                return;

            /* $2A: DAC data (8-bit signed PCM for channel 6 DAC mode) */
            case 0x2A:
                ym->dac_data = data;
                ym->regs[0][0x2A] = data;
                return;

            /* $2B: DAC enable (bit 7 = 1 enables DAC on ch6) */
            case 0x2B:
                ym->dac_enable = (data >> 7) & 1;
                ym->regs[0][0x2B] = data;
                return;

            default:
                break;
        }
    }

    /* --- Per-operator registers ($30-$9F) ------------------------------- */
    /* Each channel has 4 operators, each operator has 8 register
     * offsets within the slot:
     *   $30+s*4+o  DT1/MUL   (s=slot base, o=0..3 operator)
     *   $40+s*4+o  TL
     *   $50+s*4+o  KS/AR
     *   $60+s*4+o  AM/DR
     *   $70+s*4+o  SR
     *   $80+s*4+o  SL/RR
     *   $90+s*4+o  SSG-EG
     *
     * Slot base (s) is channel*4 within the bank. */
    if (reg >= 0x30u && reg <= 0x9Fu) {
        ym->regs[bank][reg] = data;
        return;
    }

    /* --- Per-channel registers ($A0-$BF) -------------------------------- */

    /* $A0-$A3: Frequency LSB per channel (bank 0: ch1-3, bank 1: ch4-6) */
    if (reg >= 0xA0u && reg <= 0xA3u) {
        ym->regs[bank][reg] = data;
        return;
    }

    /* $A4-$A7: Block (3 bits) + Frequency MSB (3 bits) per channel */
    if (reg >= 0xA4u && reg <= 0xA7u) {
        ym->regs[bank][reg] = data;
        return;
    }

    /* $A8-$AB: Ch3 supplemental frequency LSB (bank 0 only) */
    if (bank == 0 && reg >= 0xA8u && reg <= 0xABu) {
        ym->regs[0][reg] = data;
        return;
    }

    /* $AC-$AF: Ch3 supplemental block + freq MSB (bank 0 only) */
    if (bank == 0 && reg >= 0xACu && reg <= 0xAFu) {
        ym->regs[0][reg] = data;
        return;
    }

    /* $B0-$B3: Feedback (bits 6-3) / Algorithm (bits 2-0) per channel */
    if (reg >= 0xB0u && reg <= 0xB3u) {
        ym->regs[bank][reg] = data;
        return;
    }

    /* $B4-$B7: Stereo pan (LR), AMS, FMS per channel
     *   bit 7 = L output enable
     *   bit 6 = R output enable
     *   bit 5-4 = AMS sensitivity
     *   bit 3-2 = (unused on YM2612)
     *   bit 1-0 = FMS sensitivity */
    if (reg >= 0xB4u && reg <= 0xB7u) {
        ym->regs[bank][reg] = data;
        return;
    }

    /* All other registers: write anyway for debug inspection, but
     * real YM2612 ignores them. */
    ym->regs[bank][reg] = data;
}

dk_u8 dk_ym2612_read(dk_ym2612_t *ym, int bank)
{
    if (ym == 0) return 0xFFu;
    if (bank < 0 || bank >= DK_YM_BANKS) return 0xFFu;

    /* Most YM2612 registers are write-only.  Reading the data port
     * returns the status byte on real hardware. */
    return dk_ym2612_read_status(ym);
}

dk_u8 dk_ym2612_read_status(dk_ym2612_t *ym)
{
    dk_u8 status = 0;
    if (ym == 0) return 0xFFu;

    /* Bit 7: BUSY flag — set if chip is still processing a write */
    if (ym->busy_cycles > 0) {
        status |= 0x80u;
    }

    /* Bit 0: Timer A overflow */
    if (ym->timer_a_cnt <= 0 && (ym->timer_ctrl & 0x40u)) {
        status |= 0x01u;
    }

    /* Bit 1: Timer B overflow */
    if (ym->timer_b_cnt <= 0 && (ym->timer_ctrl & 0x08u)) {
        status |= 0x02u;
    }

    return status;
}

void dk_ym2612_update(dk_ym2612_t *ym, int cycles)
{
    if (ym == 0) return;

    ym->cycles += (dk_u32)cycles;

    /* Decrement busy counter */
    if (ym->busy_cycles > 0) {
        ym->busy_cycles -= cycles;
        if (ym->busy_cycles < 0) ym->busy_cycles = 0;
    }

    /* Timer A: decrements every 18 cycles (prescaled from YM2612 clock).
     * When counter hits 0 and timer is enabled, the overflow flag is set. */
    if (ym->timer_ctrl & 0x40u) {  /* Timer A enable */
        ym->timer_a_cnt -= cycles / 18;
        if (ym->timer_a_cnt <= 0) {
            /* Overflow — reload if load bit set */
            if (ym->timer_ctrl & 0x80u) {
                ym->timer_a_cnt = ym->timer_a;
            }
        }
    }

    /* Timer B: decrements every 288 cycles (prescaled × 16). */
    if (ym->timer_ctrl & 0x08u) {  /* Timer B enable */
        ym->timer_b_cnt -= cycles / 288;
        if (ym->timer_b_cnt <= 0) {
            if (ym->timer_ctrl & 0x10u) {
                ym->timer_b_cnt = ym->timer_b;
            }
        }
    }

}
