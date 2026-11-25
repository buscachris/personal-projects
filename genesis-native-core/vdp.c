/*
 * vdp.c — Genesis/Mega Drive Video Display Processor (VDP) implementation
 *
 * YM7101 VDP: VRAM, CRAM, VSRAM, 24 registers, DMA, VBlank/HBlank.
 * Pure C89, uses dk_u8/dk_u16/dk_u32 from devkity_native.h.
 */

#include "vdp.h"
#include <string.h>

/* ================================================================== */
/*  Internal helpers                                                   */
/* ================================================================== */

/* H40/H32 mode from register 12 (bit 0: 1=H40, 0=H32) */
static int dk_vdp_is_h40(const dk_vdp_t *vdp)
{
    return (vdp->regs[12] & 0x01u) != 0u;
}

/* DMA enable bit in register 1 (bit 4) */
static int dk_vdp_dma_enabled(const dk_vdp_t *vdp)
{
    return (vdp->regs[1] & 0x10u) != 0u;
}

/* Display enable bit in register 1 (bit 6) */
static int dk_vdp_display_is_enabled(const dk_vdp_t *vdp)
{
    return (vdp->regs[1] & 0x40u) != 0u;
}

/* VBlank interrupt enable in register 1 (bit 5) */
static int dk_vdp_vblank_int_enabled(const dk_vdp_t *vdp)
{
    return (vdp->regs[1] & 0x20u) != 0u;
}

/* HBlank interrupt enable in register 0 (bit 4) */
static int dk_vdp_hblank_int_enabled(const dk_vdp_t *vdp)
{
    return (vdp->regs[0] & 0x10u) != 0u;
}

/* Reconstruct DMA source address from registers 21-23 */
static dk_u32 dk_vdp_compute_dma_source(const dk_vdp_t *vdp)
{
    /* Reg 21: source low 8 bits
     * Reg 22: source mid 8 bits
     * Reg 23: source high 6 bits (bits 5-0) + DMA mode (bits 7-6)
     * Source = (reg23 & 0x3F) << 17 | reg22 << 9 | reg21 << 1 */
    return ((dk_u32)(vdp->regs[23] & 0x7Fu) << 17)
         | ((dk_u32)vdp->regs[22] << 9)
         | ((dk_u32)vdp->regs[21] << 1);
}

/* Reconstruct DMA length from registers 19-20 */
static dk_u16 dk_vdp_compute_dma_length(const dk_vdp_t *vdp)
{
    return (dk_u16)(((dk_u16)vdp->regs[20] << 8) | (dk_u16)vdp->regs[19]);
}

/* Extract DMA mode from register 23 (bits 7-6) */
static dk_u8 dk_vdp_compute_dma_mode(const dk_vdp_t *vdp)
{
    return (dk_u8)((vdp->regs[23] >> 6) & 0x03u);
}

/* Perform a register write with mask validation */
static void dk_vdp_write_reg(dk_vdp_t *vdp, int reg, dk_u8 value)
{
    if (reg < 0 || reg >= (int)DK_VDP_REG_COUNT) {
        return;
    }
    /* Preserve the software-visible byte. Rendering helpers apply the
     * register-specific address masks where those bits are consumed. */
    vdp->regs[reg] = (dk_u16)value;
}

/* Read a word from VRAM at byte address (big-endian) */
static dk_u16 dk_vdp_read_vram_word(const dk_vdp_t *vdp, dk_u16 addr)
{
    dk_u16 wa = (dk_u16)(addr & 0xFFFEu);
    if (wa + 1u >= DK_VDP_VRAM_SIZE) {
        return 0u;
    }
    return (dk_u16)(((dk_u16)vdp->vram[wa] << 8) | (dk_u16)vdp->vram[wa + 1u]);
}

/* Write a word to VRAM at byte address (big-endian) */
static void dk_vdp_write_vram_word(dk_vdp_t *vdp, dk_u16 addr, dk_u16 value)
{
    dk_u16 wa = (dk_u16)(addr & 0xFFFEu);
    if (wa + 1u >= DK_VDP_VRAM_SIZE) {
        return;
    }
    vdp->vram[wa]     = (dk_u8)((value >> 8) & 0xFFu);
    vdp->vram[wa + 1u] = (dk_u8)(value & 0xFFu);
}

/* Read a word from CRAM at byte address */
static dk_u16 dk_vdp_read_cram_word(const dk_vdp_t *vdp, dk_u8 addr)
{
    dk_u8 wa = (dk_u8)(addr & 0x7Eu);
    if (wa + 1u >= DK_VDP_CRAM_SIZE) {
        return 0u;
    }
    return (dk_u16)(((dk_u16)vdp->cram[wa] << 8) | (dk_u16)vdp->cram[wa + 1u]);
}

/* Write a word to CRAM at byte address (6-bit BGR: 0EEE0EEE0EEE) */
static void dk_vdp_write_cram_word(dk_vdp_t *vdp, dk_u8 addr, dk_u16 value)
{
    dk_u8 wa = (dk_u8)(addr & 0x7Eu);
    if (wa + 1u >= DK_VDP_CRAM_SIZE) {
        return;
    }
    value &= 0x0EEEu;
    vdp->cram[wa]     = (dk_u8)((value >> 8) & 0xFFu);
    vdp->cram[wa + 1u] = (dk_u8)(value & 0xFFu);
}

/* Read a word from VSRAM at byte address */
static dk_u16 dk_vdp_read_vsram_word(const dk_vdp_t *vdp, dk_u8 addr)
{
    dk_u8 wa = (dk_u8)(addr & 0x7Eu);
    if (wa + 1u >= DK_VDP_VSRAM_SIZE) {
        return 0u;
    }
    return (dk_u16)(((dk_u16)vdp->vsram[wa] << 8) | (dk_u16)vdp->vsram[wa + 1u]);
}

/* Write a word to VSRAM at byte address (10-bit: mask 0x03FF) */
static void dk_vdp_write_vsram_word(dk_vdp_t *vdp, dk_u8 addr, dk_u16 value)
{
    dk_u8 wa = (dk_u8)(addr & 0x7Eu);
    if (wa + 1u >= DK_VDP_VSRAM_SIZE) {
        return;
    }
    value &= 0x03FFu;
    vdp->vsram[wa]     = (dk_u8)((value >> 8) & 0xFFu);
    vdp->vsram[wa + 1u] = (dk_u8)(value & 0xFFu);
}

/* Apply auto-increment to current address (reg 15). */
static void dk_vdp_auto_inc(dk_vdp_t *vdp)
{
    dk_u8 inc = (dk_u8)(vdp->regs[15] & 0xFFu);
    if (inc == 0u) {
        inc = 2u;
    }
    vdp->address = (dk_u16)((vdp->address + (dk_u16)inc) & 0xFFFFu);
}

/* ================================================================== */
/*  dk_vdp_init — Power-on reset                                       */
/* ================================================================== */
void dk_vdp_init(dk_vdp_t *vdp)
{
    if (vdp == 0) {
        return;
    }
    memset(vdp, 0, sizeof(*vdp));

    /* Power-on register defaults */
    vdp->regs[0]  = 0x04u;
    vdp->regs[1]  = 0x04u;
    vdp->regs[2]  = 0x30u;
    vdp->regs[3]  = 0x00u;
    vdp->regs[4]  = 0x00u;
    vdp->regs[5]  = 0x00u;
    vdp->regs[7]  = 0x00u;
    vdp->regs[10] = 0xFFu;
    vdp->regs[11] = 0x00u;
    vdp->regs[12] = 0x00u;
    vdp->regs[13] = 0x00u;
    vdp->regs[15] = 0x00u;
    vdp->regs[16] = 0x00u;
    vdp->regs[17] = 0x00u;

    vdp->command_word  = 0u;
    vdp->address       = 0u;
    vdp->code          = 0u;
    vdp->first_half    = 0u;
    vdp->display_enabled = 0u;
    vdp->vblank_active  = 0u;
    vdp->hblank_active  = 0u;
    vdp->vblank_pending = 0u;
    vdp->hblank_pending = 0u;
    vdp->scanline       = 0u;
    vdp->hcounter       = 0u;
    vdp->fifo_full      = 0u;
    vdp->fifo_empty     = 1u;
    vdp->dma_source     = 0u;
    vdp->dma_length     = 0u;
    vdp->dma_mode       = 0u;
    vdp->dma_pending    = 0u;
    vdp->dma_running    = 0u;
}

/* ================================================================== */
/*  dk_vdp_write_control — Write to $C00004 (control port)             */
/*                                                                      */
/*  Register writes ($8xxx) are complete in one 16-bit write. Other    */
/*  commands pair a first/MSW word with a second/LSW word.              */
/* ================================================================== */
void dk_vdp_write_control(dk_vdp_t *vdp, dk_u16 value)
{
    int reg_num;
    dk_u8 reg_val;
    dk_u32 addr;

    if (vdp == 0) {
        return;
    }

    if ((value & 0xC000u) == 0x8000u) {
        vdp->first_half = 0u;
        vdp->command_word = 0u;
        reg_num = (int)((value >> 8) & 0x1Fu);
        reg_val = (dk_u8)(value & 0xFFu);
        dk_vdp_write_reg(vdp, reg_num, reg_val);
        vdp->display_enabled = dk_vdp_display_is_enabled(vdp) ? 1u : 0u;
        return;
    }

    if (vdp->first_half == 0u) {
        vdp->command_word = (dk_u32)value << 16;
        vdp->first_half = 1u;
        return;
    }

    vdp->command_word |= (dk_u32)value;
    vdp->first_half = 0u;

    addr = ((vdp->command_word >> 16) & 0x3FFFu)
         | ((vdp->command_word & 0x0003u) << 14);
    vdp->code = (dk_u8)(
        ((vdp->command_word >> 30) & 0x03u)
        | ((vdp->command_word >> 2) & 0x3Cu)
    );
    if ((vdp->code & 0x0Fu) == DK_VDP_CMD_CRAM_WRITE
        || (vdp->code & 0x0Fu) == DK_VDP_CMD_CRAM_READ) {
        addr &= 0x007Eu;
    } else if ((vdp->code & 0x0Fu) == DK_VDP_CMD_VSRAM_WRITE
               || (vdp->code & 0x0Fu) == DK_VDP_CMD_VSRAM_READ) {
        addr &= 0x004Eu;
    }
    vdp->address = (dk_u16)(addr & 0xFFFFu);

    if (dk_vdp_dma_enabled(vdp) && (vdp->code & 0x20u) != 0u) {
        vdp->dma_source = dk_vdp_compute_dma_source(vdp);
        vdp->dma_length = dk_vdp_compute_dma_length(vdp);
        vdp->dma_mode = dk_vdp_compute_dma_mode(vdp);
        if (vdp->dma_length != 0u) {
            vdp->dma_pending = 1u;
            vdp->dma_running = 1u;
        }
    }
}

/* ================================================================== */
/*  dk_vdp_write_data — Write to $C00000 (data port)                   */
/*  Writes 16-bit word to addressed VRAM/CRAM/VSRAM with auto-inc.     */
/* ================================================================== */
void dk_vdp_write_data(dk_vdp_t *vdp, dk_u16 value)
{
    if (vdp == 0) {
        return;
    }

    vdp->fifo_empty = 0u;

    switch (vdp->code & 0x0Fu) {
        case DK_VDP_CMD_VRAM_WRITE:
            dk_vdp_write_vram_word(vdp, vdp->address, value);
            break;
        case DK_VDP_CMD_CRAM_WRITE:
            dk_vdp_write_cram_word(vdp, (dk_u8)(vdp->address & 0x7Eu), value);
            break;
        case DK_VDP_CMD_VSRAM_WRITE:
            dk_vdp_write_vsram_word(vdp, (dk_u8)(vdp->address & 0x7Eu), value);
            break;
        default:
            break;
    }

    dk_vdp_auto_inc(vdp);
    vdp->fifo_empty = 1u;
}

/* ================================================================== */
/*  dk_vdp_read_data — Read from $C00000 (data port)                   */
/*  Reads 16-bit word from addressed VRAM/CRAM/VSRAM with auto-inc.    */
/* ================================================================== */
dk_u16 dk_vdp_read_data(dk_vdp_t *vdp)
{
    dk_u16 result = 0u;

    if (vdp == 0) {
        return 0u;
    }

    switch (vdp->code & 0x0Fu) {
        case DK_VDP_CMD_VRAM_READ:
        case DK_VDP_CMD_VRAM_READ_8:
            result = dk_vdp_read_vram_word(vdp, vdp->address);
            break;
        case DK_VDP_CMD_CRAM_READ:
            result = dk_vdp_read_cram_word(vdp, (dk_u8)(vdp->address & 0x7Eu));
            break;
        case DK_VDP_CMD_VSRAM_READ:
            result = dk_vdp_read_vsram_word(vdp, (dk_u8)(vdp->address & 0x7Eu));
            break;
        default:
            break;
    }

    dk_vdp_auto_inc(vdp);
    return result;
}

/* ================================================================== */
/*  dk_vdp_read_control — Read from $C00004 (HV counter)               */
/*  V counter in bits 15-8, H counter in bits 7-0.                     */
/* ================================================================== */
dk_u16 dk_vdp_read_control(dk_vdp_t *vdp)
{
    dk_u16 v_counter;
    dk_u16 h_counter;

    if (vdp == 0) {
        return 0u;
    }

    vdp->first_half = 0u;

    v_counter = (dk_u16)(vdp->scanline & 0xFFu);

    if (dk_vdp_is_h40(vdp)) {
        h_counter = (dk_u16)((vdp->hcounter * 256u) / DK_VDP_H40_COUNTERS);
    } else {
        h_counter = (dk_u16)((vdp->hcounter * 256u) / DK_VDP_H32_COUNTERS);
    }

    return (dk_u16)((v_counter << 8) | (h_counter & 0xFFu));
}

/* ================================================================== */
/*  dk_vdp_read_status — 16-bit Genesis status in the low word.        */
/* ================================================================== */
dk_u32 dk_vdp_read_status(dk_vdp_t *vdp)
{
    dk_u32 status = 0u;

    if (vdp == 0) {
        return 0u;
    }

    vdp->first_half = 0u;

    if (vdp->fifo_full) {
        status |= 0x0100u;
    }
    if (vdp->fifo_empty) {
        status |= 0x0200u;
    }
    if (vdp->vblank_pending) {
        status |= 0x0080u;
    }
    if (vdp->vblank_active) {
        status |= 0x0008u;
    }
    if (vdp->hblank_active) {
        status |= 0x0004u;
    }
    if (vdp->dma_running) {
        status |= 0x0002u;
    }

    return status;
}

/* ================================================================== */
/*  dk_vdp_hblank — End-of-scanline HBlank processing                  */
/*  Decrements HBlank interrupt counter (reg 10) and sets              */
/*  HBlank interrupt pending on underflow.                             */
/* ================================================================== */
void dk_vdp_hblank(dk_vdp_t *vdp)
{
    if (vdp == 0) {
        return;
    }

    vdp->hblank_active = 1u;

    /* HBlank interrupt counter */
    if (dk_vdp_hblank_int_enabled(vdp)) {
        dk_u8 hcval = (dk_u8)(vdp->regs[10] & 0xFFu);
        if (hcval == 0u) {
            vdp->hblank_pending = 1u;
        }
        vdp->regs[10] = (dk_u16)(((dk_u16)vdp->regs[10] - 1u) & 0xFFu);
    }
}

/* ================================================================== */
/*  dk_vdp_vblank — Start of VBlank period (scanline 224)              */
/*  Asserts VBlank interrupt pending if enabled (reg 1 bit 5).         */
/* ================================================================== */
void dk_vdp_vblank(dk_vdp_t *vdp)
{
    if (vdp == 0) {
        return;
    }

    vdp->vblank_active = 1u;

    if (dk_vdp_vblank_int_enabled(vdp)) {
        vdp->vblank_pending = 1u;
    }
}

/* ================================================================== */
/*  dk_vdp_scanline_step — Advance one scanline                        */
/*  Handles HBlank, VBlank transitions, and interrupt counter reload.  */
/* ================================================================== */
void dk_vdp_scanline_step(dk_vdp_t *vdp)
{
    if (vdp == 0) {
        return;
    }

    /* End of current scanline: trigger HBlank */
    dk_vdp_hblank(vdp);

    /* Clear HBlank at start of next scanline (after a brief period) */
    vdp->hblank_active = 0u;

    /* Advance scanline */
    vdp->scanline = (dk_u16)((vdp->scanline + 1u) % DK_VDP_SCANLINE_TOTAL);

    /* Enter VBlank at scanline 224 */
    if (vdp->scanline == DK_VDP_SCANLINE_VBLANK_START) {
        dk_vdp_vblank(vdp);
    }

    /* Exit VBlank at scanline 0 (start of new frame) */
    if (vdp->scanline == 0u) {
        vdp->vblank_active = 0u;
        vdp->vblank_pending = 0u;
        /* Reload HBlank int counter from reg 10 */
        vdp->regs[10] = (dk_u16)(vdp->regs[10] & 0xFFu);
    }

    /* Reset H counter for new scanline */
    vdp->hcounter = 0u;

    /* DMA completes at end of VBlank (scanline 0) or when length expires */
    if (vdp->scanline == 0u) {
        vdp->dma_running = 0u;
        vdp->dma_pending = 0u;
    }
}

/* ================================================================== */
/*  dk_vdp_get_vblank_irq — Check VBlank interrupt                     */
/*  Returns non-zero if level 6 VBlank interrupt should be asserted.   */
/* ================================================================== */
int dk_vdp_get_vblank_irq(dk_vdp_t *vdp)
{
    if (vdp == 0) {
        return 0;
    }
    if (vdp->vblank_pending && dk_vdp_vblank_int_enabled(vdp)) {
        return 1;
    }
    return 0;
}

/* ================================================================== */
/*  dk_vdp_get_hblank_irq — Check HBlank interrupt                     */
/*  Returns non-zero if level 4 HBlank interrupt should be asserted.   */
/* ================================================================== */
int dk_vdp_get_hblank_irq(dk_vdp_t *vdp)
{
    if (vdp == 0) {
        return 0;
    }
    if (vdp->hblank_pending && dk_vdp_hblank_int_enabled(vdp)) {
        return 1;
    }
    return 0;
}

/* ================================================================== */
/*  dk_vdp_dma_execute — Execute pending DMA transfer                  */
/*                                                                      */
/*  Takes a callback to read 16-bit words from the 68k bus.            */
/*  DMA mode 0: 68k → VRAM (word writes to current address)            */
/*  DMA mode 1: VRAM → VRAM copy (byte-level)                         */
/*  DMA mode 2: 68k → CRAM                                             */
/*  DMA mode 3: 68k → VSRAM                                            */
/*                                                                      */
/*  After transfer, auto-increment is NOT applied (DMA handles its     */
/*  own addressing). DMA busy flag is cleared on completion.            */
/* ================================================================== */
void dk_vdp_dma_execute(
    dk_vdp_t *vdp,
    dk_vdp_read_word_fn read_word,
    void *context
)
{
    dk_u16 i;
    dk_u16 len;
    dk_u32 src;
    dk_u16 dst;
    dk_u16 word;

    if (vdp == 0 || read_word == 0) {
        return;
    }
    if (!vdp->dma_pending) {
        return;
    }

    len = vdp->dma_length;
    if (len == 0u) {
        vdp->dma_pending = 0u;
        vdp->dma_running = 0u;
        return;
    }

    src = vdp->dma_source;
    dst = vdp->address;

    switch (vdp->dma_mode) {
        case DK_DMA_MODE_68K_TO_VDP_0:
        case DK_DMA_MODE_68K_TO_VDP_1:
            for (i = 0u; i < len; ++i) {
                word = read_word(context, src & 0x00FFFFFFu);
                switch (vdp->code & 0x0Fu) {
                    case DK_VDP_CMD_CRAM_WRITE:
                        dk_vdp_write_cram_word(vdp, (dk_u8)(dst & 0x7Eu), word);
                        break;
                    case DK_VDP_CMD_VSRAM_WRITE:
                        dk_vdp_write_vsram_word(vdp, (dk_u8)(dst & 0x7Eu), word);
                        break;
                    default:
                        dk_vdp_write_vram_word(vdp, dst, word);
                        break;
                }
                src = (src + 2u) & 0x00FFFFFFu;
                dst = (dk_u16)((dst + (dk_u16)(vdp->regs[15] ? vdp->regs[15] : 2u)) & 0xFFFFu);
            }
            break;

        case DK_DMA_MODE_VRAM_COPY:
            /* Mode 1: VRAM → VRAM copy
             * Source is byte address from DMA source registers
             * Copies 'len' bytes (not words) from VRAM[source] to VRAM[dest]
             * Both source and destination increment by 1 */
            for (i = 0u; i < len; ++i) {
                dk_u8 byte_val;
                dk_u16 src_idx = (dk_u16)(src & 0xFFFFu);
                dk_u16 dst_idx = (dk_u16)(dst & 0xFFFFu);
                /* src_idx and dst_idx are dk_u16 (0-65535),
                   VRAM is 64KB so index is always valid */
                byte_val = vdp->vram[src_idx];
                vdp->vram[dst_idx] = byte_val;
                src = (src + 1u) & 0x00FFFFFFu;
                dst = (dk_u16)((dst + 1u) & 0xFFFFu);
            }
            break;

        case DK_DMA_MODE_VRAM_FILL:
            /* Fill begins with the following data-port write. */
            break;

        default:
            break;
    }

    /* Update address register to final destination */
    vdp->address = dst;

    /* Clear DMA state */
    vdp->dma_pending = 0u;
    vdp->dma_running = 0u;
}

/* ================================================================== */
/*  dk_vdp_get_display_width — Current horizontal resolution            */
/*  Returns 320 for H40 mode, 256 for H32 mode.                       */
/* ================================================================== */
dk_u16 dk_vdp_get_display_width(const dk_vdp_t *vdp)
{
    if (vdp == 0) {
        return DK_VDP_H40_WIDTH;
    }
    return dk_vdp_is_h40(vdp) ? (dk_u16)DK_VDP_H40_WIDTH
                               : (dk_u16)DK_VDP_H32_WIDTH;
}

/* ================================================================== */
/*  dk_vdp_get_bg_color — Background color from CRAM (register 7)      */
/*  Returns the CRAM word for the background color.                    */
/*  Reg 7 format: PPP CCC (palette 0-3, color 0-63 within palette)    */
/*  Background color index = reg7 & 0x3F (Mode 5)                     */
/* ================================================================== */
dk_u16 dk_vdp_get_bg_color(const dk_vdp_t *vdp)
{
    dk_u8 color_index;
    if (vdp == 0) {
        return 0u;
    }
    /* Background color: reg 7 selects CRAM entry 0-63 */
    color_index = (dk_u8)(vdp->regs[7] & 0x3Fu);
    return dk_vdp_read_cram_word(vdp, (dk_u8)((color_index << 1) & 0x7Eu));
}
