/*
 * vdp.h — Genesis/Mega Drive Video Display Processor (VDP)
 *
 * Implements the YM7101 VDP as used in the Sega Genesis/Mega Drive:
 *   - 64KB VRAM, 128-byte CRAM, 80-byte VSRAM
 *   - 24 write registers with Genesis-specific write masks
 *   - 32-bit command word parsing (control port $C00004)
 *   - DMA: 68k→VRAM, VRAM copy, 68k→CRAM/VSRAM (auto-trigger)
 *   - VBlank/HBlank interrupt generation (level 6 / level 4)
 *   - Status register with FIFO flags, DMA busy, VBlank/HBlank status
 *
 * Pure C89, no dependencies beyond the dk_u8/dk_u16/dk_u32 types
 * from devkity_native.h.
 */

#ifndef DK_VDP_H
#define DK_VDP_H

#include "dk_types.h"

/* ------------------------------------------------------------------ */
/*  VDP memory sizes                                                  */
/* ------------------------------------------------------------------ */
#define DK_VDP_VRAM_SIZE   0x10000u  /* 64 KB */
#define DK_VDP_CRAM_SIZE   128u      /* 64 entries × 2 bytes */
#define DK_VDP_VSRAM_SIZE  80u       /* 40 entries × 2 bytes */
#define DK_VDP_REG_COUNT   24u       /* Registers 0-23 */

/* ------------------------------------------------------------------ */
/*  Display dimensions                                                */
/* ------------------------------------------------------------------ */
#define DK_VDP_H40_WIDTH   320u
#define DK_VDP_H32_WIDTH   256u
#define DK_VDP_HEIGHT      224u
#define DK_VDP_FRAMEBUFFER_SIZE  (320u * 224u)

/* ------------------------------------------------------------------ */
/*  Scanline limits (NTSC)                                            */
/* ------------------------------------------------------------------ */
#define DK_VDP_SCANLINE_VBLANK_START  224u   /* First VBlank scanline */
#define DK_VDP_SCANLINE_TOTAL        262u    /* Total NTSC scanlines */
#define DK_VDP_H40_COUNTERS          420u   /* HCounter max in H40 */
#define DK_VDP_H32_COUNTERS          342u   /* HCounter max in H32 */

/* Low-nibble VDP command codes after decoding the two control words. */
#define DK_VDP_CMD_VRAM_READ    0x00u
#define DK_VDP_CMD_VRAM_WRITE   0x01u
#define DK_VDP_CMD_CRAM_WRITE   0x03u
#define DK_VDP_CMD_VSRAM_READ   0x04u
#define DK_VDP_CMD_VSRAM_WRITE  0x05u
#define DK_VDP_CMD_CRAM_READ    0x08u
#define DK_VDP_CMD_VRAM_READ_8  0x0Cu

/* ------------------------------------------------------------------ */
/*  DMA transfer modes (bits 7-6 of register 23)                     */
/* ------------------------------------------------------------------ */
#define DK_DMA_MODE_68K_TO_VDP_0   0u
#define DK_DMA_MODE_68K_TO_VDP_1   1u
#define DK_DMA_MODE_VRAM_FILL      2u
#define DK_DMA_MODE_VRAM_COPY      3u

/* ------------------------------------------------------------------ */
/*  Interrupt levels                                                  */
/* ------------------------------------------------------------------ */
#define DK_VDP_VBLANK_LEVEL  6u
#define DK_VDP_HBLANK_LEVEL  4u

/* ------------------------------------------------------------------ */
/*  VDP state structure                                               */
/* ------------------------------------------------------------------ */
typedef struct dk_vdp_s {
    /* Memory */
    dk_u8  vram[DK_VDP_VRAM_SIZE];       /* 64 KB video RAM          */
    dk_u8  cram[DK_VDP_CRAM_SIZE];       /* 128 bytes palette RAM    */
    dk_u8  vsram[DK_VDP_VSRAM_SIZE];     /* 80 bytes vertical scroll */

    /* Registers */
    dk_u16 regs[DK_VDP_REG_COUNT];       /* 24 VDP write registers   */

    /* Command port state */
    dk_u32 command_word;                  /* Latched 32-bit command   */
    dk_u16 address;                       /* Current VRAM/CRAM/VSRAM address */
    dk_u8  code;                          /* Command code (0=VRAM,1=CRAM,2=VSRAM) */
    dk_u8  first_half;                    /* Tracking for 32-bit command assembly */

    /* Display state */
    dk_u8  display_enabled;              /* Display enable (reg 1 bit 6)   */
    dk_u8  vblank_active;                /* In VBlank region (scanline >= 224) */
    dk_u8  hblank_active;                /* In HBlank region (within scanline) */
    dk_u8  vblank_pending;               /* VBlank interrupt pending        */
    dk_u8  hblank_pending;               /* HBlank interrupt pending        */
    dk_u16 scanline;                     /* Current scanline (0-261 NTSC)   */
    dk_u16 hcounter;                     /* Horizontal counter             */

    /* FIFO state (simplified) */
    dk_u8  fifo_full;                    /* FIFO full flag (status bit 7)  */
    dk_u8  fifo_empty;                   /* FIFO empty flag (status bit 8) */

    /* DMA state */
    dk_u32 dma_source;                   /* DMA source address (68k bus)  */
    dk_u16 dma_length;                   /* DMA transfer length           */
    dk_u8  dma_mode;                     /* DMA mode (0-3)                */
    dk_u8  dma_pending;                  /* DMA transfer is pending/active */
    dk_u8  dma_running;                  /* DMA is currently executing     */

    /* Frame buffer output (RGBA8888) */
    dk_u32 frame_buffer[DK_VDP_FRAMEBUFFER_SIZE];
} dk_vdp_t;

/* ------------------------------------------------------------------ */
/*  Public interface                                                  */
/* ------------------------------------------------------------------ */

/* Initialize VDP to power-on state */
void dk_vdp_init(dk_vdp_t *vdp);

/* Write 16-bit value to control port ($C00004) */
void dk_vdp_write_control(dk_vdp_t *vdp, dk_u16 value);

/* Write 16-bit value to data port ($C00000) */
void dk_vdp_write_data(dk_vdp_t *vdp, dk_u16 value);

/* Read 16-bit value from control port ($C00004) — returns HV counter */
dk_u16 dk_vdp_read_control(dk_vdp_t *vdp);

/* Read 16-bit value from data port ($C00000) */
dk_u16 dk_vdp_read_data(dk_vdp_t *vdp);

/* Read 32-bit VDP status register */
dk_u32 dk_vdp_read_status(dk_vdp_t *vdp);

/* Process HBlank event (end of scanline display) */
void dk_vdp_hblank(dk_vdp_t *vdp);

/* Process VBlank event (end of active display) */
void dk_vdp_vblank(dk_vdp_t *vdp);

/* Advance one scanline step */
void dk_vdp_scanline_step(dk_vdp_t *vdp);

/* Check if VBlank interrupt should be asserted (returns non-zero if yes) */
int dk_vdp_get_vblank_irq(dk_vdp_t *vdp);

/* Check if HBlank interrupt should be asserted (returns non-zero if yes) */
int dk_vdp_get_hblank_irq(dk_vdp_t *vdp);

/* Execute pending DMA transfer (called from 68k bus context) */
typedef dk_u16 (*dk_vdp_read_word_fn)(void *context, dk_u32 addr);
void dk_vdp_dma_execute(
    dk_vdp_t *vdp,
    dk_vdp_read_word_fn read_word,
    void *context
);

/* Get the current display width based on H40/H32 mode */
dk_u16 dk_vdp_get_display_width(const dk_vdp_t *vdp);

/* Get background color from CRAM (register 7) */
dk_u16 dk_vdp_get_bg_color(const dk_vdp_t *vdp);

#endif /* DK_VDP_H */
