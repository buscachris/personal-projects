/*
 * render.h — Genesis/Mega Drive Scanline Renderer
 *
 * Renders the VDP display output (nametables, sprites, scrolling)
 * into a 32-bit RGBA frame buffer. Implements the full Genesis
 * priority ordering: B → A(non-pri) → Sprites(non-pri) → A(pri) → Sprites(pri)
 *
 * Pure C89, no dependencies beyond dk_u8/dk_u16/dk_u32 from devkity_native.h
 * and the dk_vdp_t struct from vdp.h.
 */

#ifndef DK_RENDER_H
#define DK_RENDER_H

#include "vdp.h"

/* ------------------------------------------------------------------ */
/*  Render dimensions                                                  */
/* ------------------------------------------------------------------ */
#define DK_RENDER_WIDTH   320u   /* Max width (H40 mode)   */
#define DK_RENDER_HEIGHT  224u   /* NTSC active lines      */

/* ------------------------------------------------------------------ */
/*  Scanline limits                                                    */
/* ------------------------------------------------------------------ */
#define DK_RENDER_SPRITES_PER_LINE  20u   /* Max sprites per scanline */

/* ------------------------------------------------------------------ */
/*  Plane and tile constants                                           */
/* ------------------------------------------------------------------ */
#define DK_RENDER_TILE_SIZE      8u      /* 8×8 pixels per tile       */
#define DK_RENDER_TILE_BYTES    32u     /* 32 bytes per tile in VRAM */
#define DK_RENDER_PLANE_WIDTH_CELLS  64u   /* 64 cells = 512 pixels */
#define DK_RENDER_PLANE_HEIGHT_CELLS 32u   /* 32 cells = 256 pixels */
#define DK_RENDER_SPRITE_COUNT   80u     /* Max sprites on-screen     */

/* ------------------------------------------------------------------ */
/*  Render state structure                                             */
/* ------------------------------------------------------------------ */
typedef struct dk_render_s {
    dk_u32 frame_buffer[DK_RENDER_WIDTH * DK_RENDER_HEIGHT];
    int    width;       /* Current display width (320 or 256) */
    int    height;      /* Current display height (224)       */
} dk_render_t;

/* ------------------------------------------------------------------ */
/*  Public interface                                                   */
/* ------------------------------------------------------------------ */

/* Initialize renderer to default state */
void dk_render_init(dk_render_t *ren);

/* Render a single scanline into the frame buffer */
void dk_render_scanline(dk_render_t *ren, const dk_vdp_t *vdp, int line);

/* Render a full frame (all 224 active scanlines) */
void dk_render_frame(dk_render_t *ren, const dk_vdp_t *vdp);

/* Convert a Genesis CRAM word (0x0EEE) to 32-bit RGBA (0xAARRGGBB) */
dk_u32 dk_render_cram_to_rgba(dk_u16 cram_word);

#endif /* DK_RENDER_H */
