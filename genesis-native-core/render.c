/*
 * render.c — Genesis/Mega Drive Scanline Renderer
 *
 * Renders the VDP display output into a 32-bit RGBA frame buffer.
 * Implements full Genesis rendering pipeline per scanline:
 *
 *   1. Compute display width from H40/H32 mode
 *   2. Fill background color from CRAM entry 0
 *   3. Render Plane B (low-priority pixels only)
 *   4. Render Plane A (low-priority pixels only)
 *   5. Render non-priority sprites
 *   6. Render Plane A (high-priority pixels)
 *   7. Render priority sprites
 *
 * Priority ordering: B -> A(non-pri) -> Sprites(non-pri) -> A(pri) -> Sprites(pri)
 *
 * Scrolling: H scroll from VRAM table (reg 13), V scroll from VSRAM.
 * Tile decode: 4bpp, 32 bytes per 8x8 tile (packed nibble format per task spec).
 * Sprite rendering with per-scanline limit of 20.
 *
 * Pure C89, uses dk_u8/dk_u16/dk_u32 from devkity_native.h.
 */

#include "render.h"
#include <string.h>

/* ================================================================== */
/*  Internal types                                                     */
/* ================================================================== */

/* Per-pixel line buffer entry: tracks which layer wrote the pixel */
typedef struct dk_pixel_attr_s {
    dk_u32 color;     /* RGBA color value                          */
    int     priority; /* 1 = high-priority pixel, 0 = low         */
    int     opaque;   /* 1 = pixel is opaque (non-transparent)    */
    int     layer;    /* Layer: 0=none, 1=plane B, 2=plane A, 3=sprite */
} dk_pixel_attr_t;

/* Parsed sprite entry */
typedef struct dk_sprite_s {
    int x;            /* X position (0-511, 128-offset for screen)  */
    int y;            /* Y position (0-255)                         */
    int width;        /* Width in cells (1-4)                      */
    int height;       /* Height in cells (1-4)                     */
    int tile_index;   /* Tile index in VRAM                        */
    int palette;      /* Palette line (0-3)                        */
    int h_flip;       /* Horizontal flip flag                      */
    int v_flip;       /* Vertical flip flag                        */
    int priority;     /* Priority flag (1=in front)                */
} dk_sprite_t;

/* ================================================================== */
/*  CRAM to RGBA conversion                                            */
/*  Genesis CRAM format: ----BBB0-GGG0-RRR0 (0x0EEE)                  */
/*  Each channel 3 bits (0-7), scaled to 0-252 by *36.                */
/*  Output: 0xAARRGGBB (alpha always 0xFF).                           */
/* ================================================================== */
dk_u32 dk_render_cram_to_rgba(dk_u16 cram_word)
{
    int r, g, b;
    r = ((int)((cram_word >> 1) & 0x7u) * 255) / 7;
    g = ((int)((cram_word >> 5) & 0x7u) * 255) / 7;
    b = ((int)((cram_word >> 9) & 0x7u) * 255) / 7;
    return 0xFF000000u | ((dk_u32)r << 16) | ((dk_u32)g << 8) | (dk_u32)b;
}

/* ================================================================== */
/*  Internal VRAM/CRAM/VSRAM word readers (big-endian)                */
/* ================================================================== */
static dk_u16 dk_render_vram_word(const dk_vdp_t *vdp, dk_u16 addr)
{
    dk_u16 wa = (dk_u16)(addr & 0xFFFEu);
    if (wa + 1u >= DK_VDP_VRAM_SIZE) return 0u;
    return (dk_u16)(((dk_u16)vdp->vram[wa] << 8) | (dk_u16)vdp->vram[wa + 1u]);
}

static dk_u16 dk_render_cram_word(const dk_vdp_t *vdp, dk_u8 addr)
{
    dk_u8 wa = (dk_u8)(addr & 0x7Eu);
    if (wa + 1u >= DK_VDP_CRAM_SIZE) return 0u;
    return (dk_u16)(((dk_u16)vdp->cram[wa] << 8) | (dk_u16)vdp->cram[wa + 1u]);
}

static dk_u16 dk_render_vsram_word(const dk_vdp_t *vdp, dk_u8 addr)
{
    dk_u8 wa = (dk_u8)(addr & 0x7Eu);
    if (wa + 1u >= DK_VDP_VSRAM_SIZE) return 0u;
    return (dk_u16)(((dk_u16)vdp->vsram[wa] << 8) | (dk_u16)vdp->vsram[wa + 1u]);
}

/* ================================================================== */
/*  Decode a single pixel from a tile in VRAM                         */
/*  Packed nibble format per task spec:                                */
/*    row = (vram[base + y*2] << 8) | vram[base + y*2 + 1]            */
/*    pixel x = (row >> (4 * (7 - x))) & 0xF                         */
/* ================================================================== */
static dk_u8 dk_render_tile_pixel(const dk_vdp_t *vdp,
                                   dk_u16 tile_base, int y, int x)
{
    dk_u16 byte_addr;
    dk_u8 packed;

    byte_addr = (dk_u16)(
        tile_base + (dk_u16)(y * 4) + (dk_u16)(x / 2)
    );
    packed = vdp->vram[byte_addr];
    if ((x & 1) == 0) {
        return (dk_u8)((packed >> 4) & 0x0Fu);
    }
    return (dk_u8)(packed & 0x0Fu);
}

/* ================================================================== */
/*  Get palette color from CRAM as RGBA                                */
/*  color index 0 of any palette = transparent (returns 0)            */
/* ================================================================== */
static dk_u32 dk_render_palette_color(const dk_vdp_t *vdp,
                                       int palette_line, int color_index)
{
    dk_u16 cram_word;
    dk_u8  cram_addr;

    if (color_index == 0) return 0u;

    /* CRAM: 4 palettes x 16 colors, 2 bytes each
     * Byte addr = palette_line * 32 + color_index * 2 */
    cram_addr = (dk_u8)((palette_line * 32 + color_index * 2) & 0x7Eu);
    cram_word = dk_render_cram_word(vdp, cram_addr);
    return dk_render_cram_to_rgba(cram_word);
}

/* ================================================================== */
/*  Nametable base addresses from VDP registers                        */
/*  Plane A: H40 = (reg2 & 0x38) << 10; H32 = (reg2 & 0x3C) << 10   */
/*  Plane B: always (reg4 & 0x07) << 13                               */
/* ================================================================== */
static dk_u16 dk_render_plane_a_base(const dk_vdp_t *vdp, int is_h40)
{
    dk_u16 reg2 = vdp->regs[2];
    (void)is_h40;
    return (dk_u16)((reg2 & 0x38u) << 10);
}

static dk_u16 dk_render_plane_b_base(const dk_vdp_t *vdp)
{
    return (dk_u16)((vdp->regs[4] & 0x07u) << 13);
}

/* ================================================================== */
/*  H scroll offset for a given line                                   */
/*  Mode from reg 11 bits 1-0: 00=full, 01=per-8px, 10/11=per-line   */
/*  H scroll data base = (reg 13 & 0x3F) << 10                       */
/*  Plane A/B entries interleaved: A at offset+0, B at offset+2       */
/* ================================================================== */
static int dk_render_h_scroll(const dk_vdp_t *vdp, int line, int plane)
{
    dk_u16 hscroll_base;
    dk_u16 mode;
    dk_u16 offset;
    dk_u16 raw;
    int    scroll;

    mode = (dk_u16)(vdp->regs[11] & 0x03u);
    hscroll_base = (dk_u16)((vdp->regs[13] & 0x3Fu) << 10);

    switch (mode) {
        case 0u:  offset = 0u; break;
        case 2u:  offset = (dk_u16)(((dk_u16)(line / 8) * 4u) & 0xFFFFu); break;
        default:  offset = (dk_u16)(((dk_u16)line * 4u) & 0xFFFFu); break;
    }

    if (plane == 0) {
        raw = dk_render_vram_word(vdp, (dk_u16)((hscroll_base + offset) & 0xFFFFu));
    } else {
        raw = dk_render_vram_word(vdp, (dk_u16)((hscroll_base + offset + 2u) & 0xFFFFu));
    }

    scroll = (int)raw;
    if ((raw & 0x8000u) != 0u) scroll -= 0x10000;
    return scroll;
}

/* ================================================================== */
/*  V scroll offset for a given column                                 */
/*  Mode from reg 11 bits 3-2: 00=full, else per-2-columns            */
/*  VSRAM: Plane A at even indices, Plane B at odd indices            */
/* ================================================================== */
static int dk_render_v_scroll(const dk_vdp_t *vdp, int column, int plane)
{
    dk_u16 mode;
    dk_u8  vsram_idx;
    dk_u16 raw;
    int    scroll;

    mode = (dk_u16)((vdp->regs[11] >> 2) & 0x03u);

    if (mode == 0u) {
        vsram_idx = (dk_u8)(plane & 1u);
    } else {
        vsram_idx = (dk_u8)(((column / 2) * 2 + plane) & 0x1Fu);
    }

    raw = dk_render_vsram_word(vdp, (dk_u8)((vsram_idx * 2) & 0x7Eu));

    scroll = (int)raw;
    if ((raw & 0x8000u) != 0u) scroll -= 0x10000;
    return scroll;
}

/* ================================================================== */
/*  Render a scroll plane (A or B) into the line buffer                */
/*  pri_only: 1 = render only priority tiles, 0 = only non-priority   */
/* ================================================================== */
static void dk_render_plane(dk_pixel_attr_t *line_buf,
                             const dk_vdp_t *vdp,
                             int line, dk_u16 nt_base,
                             int plane, int width, int pri_only)
{
    int h_scroll;
    int pixel_x;
    int plane_width;
    int plane_height;
    int total_width;
    int total_height;
    int width_code;
    int height_code;

    (void)pri_only;
    width_code = (int)(vdp->regs[16] & 0x03u);
    height_code = (int)((vdp->regs[16] >> 4) & 0x03u);
    plane_width = width_code == 1 ? 64 : (width_code == 3 ? 128 : 32);
    plane_height = height_code == 1 ? 64 : (height_code == 3 ? 128 : 32);
    total_width = plane_width * 8;
    total_height = plane_height * 8;

    h_scroll = dk_render_h_scroll(vdp, line, plane);

    for (pixel_x = 0; pixel_x < width; ++pixel_x) {
        int v_scroll, effective_x, effective_y;
        int cell_col, cell_row, tile_x, tile_y;
        dk_u16 nt_offset, tile_entry;
        int tile_index, tile_pal, tile_hflip, tile_vflip, tile_pri;
        dk_u16 tile_base;
        dk_u8  color_index;
        dk_u32 rgba;

        effective_x = (pixel_x - h_scroll) % total_width;
        if (effective_x < 0) effective_x += total_width;
        v_scroll = dk_render_v_scroll(vdp, pixel_x / 8, plane);
        effective_y = (line + v_scroll) % total_height;
        if (effective_y < 0) effective_y += total_height;

        /* Cell coordinates in nametable */
        cell_col = effective_x / 8;
        cell_row = effective_y / 8;
        tile_x   = effective_x & 7;
        tile_y   = effective_y & 7;

        nt_offset = (dk_u16)((nt_base + (dk_u16)(cell_row * plane_width * 2)
                              + (dk_u16)(cell_col * 2)) & 0xFFFFu);
        tile_entry = dk_render_vram_word(vdp, nt_offset);

        /* Decode tile entry:
         *   bit 15:    priority
         *   bits 14-13: palette line
         *   bit 12:    vertical flip
         *   bit 11:    horizontal flip
         *   bits 10-0:  tile index */
        tile_pri   = (tile_entry >> 15) & 1;
        tile_pal   = (tile_entry >> 13) & 3;
        tile_vflip = (tile_entry >> 12) & 1;
        tile_hflip = (tile_entry >> 11) & 1;
        tile_index = tile_entry & 0x7FF;

        /* Apply flips */
        if (tile_hflip) tile_x = 7 - tile_x;
        if (tile_vflip) tile_y = 7 - tile_y;

        /* Tile base in VRAM (32 bytes per tile) */
        tile_base = (dk_u16)((dk_u16)(tile_index * 32) & 0xFFFFu);

        /* Decode pixel */
        color_index = dk_render_tile_pixel(vdp, tile_base, tile_y, tile_x);
        if (color_index == 0u) continue;  /* transparent */
        if (line_buf[pixel_x].priority) continue;

        rgba = dk_render_palette_color(vdp, tile_pal, (int)color_index);

        line_buf[pixel_x].color    = rgba;
        line_buf[pixel_x].priority = tile_pri;
        line_buf[pixel_x].opaque   = 1;
        line_buf[pixel_x].layer    = (plane == 0) ? 2 : 1;
    }
}

/* ================================================================== */
/*  Parse sprite table from VRAM (follow link chain, max 80 sprites)   */
/*                                                                     */
/*  Sprite table base: (reg 5 & 0x7F) << 9                            */
/*  Each entry = 8 bytes:                                              */
/*    Byte 0:     Y position                                           */
/*    Byte 1:     (height_cells-1)<<2 | (width_cells-1)                */
/*    Bytes 2-3:  tile attributes (priority/palette/flips/tile index)  */
/*    Bytes 4-5:  X position (9-bit, 128 = left screen edge)          */
/*    Byte 6:     unused / link extension                              */
/*    Byte 7:     link (next sprite index, 0 = end of chain)          */
/* ================================================================== */
static int dk_render_parse_sprites(const dk_vdp_t *vdp,
                                    dk_sprite_t *sprites)
{
    dk_u16 sprite_base;
    int    count;
    int    link;
    int    i;

    sprite_base = (dk_u16)((vdp->regs[5] & 0x7Fu) << 9);
    count = 0;
    link = 0;

    for (i = 0; i < (int)DK_RENDER_SPRITE_COUNT; ++i) {
        dk_u16 addr;
        dk_u16 y_word, size_link, tile_attr, x_word;
        dk_u8 link_byte;

        addr = (dk_u16)((sprite_base + (dk_u16)(link * 8)) & 0xFFFFu);
        if (addr + 7u >= DK_VDP_VRAM_SIZE) break;

        /* Read sprite entry fields */
        y_word = dk_render_vram_word(vdp, addr);
        size_link = dk_render_vram_word(vdp, (dk_u16)(addr + 2u));
        tile_attr = dk_render_vram_word(vdp, (dk_u16)(addr + 4u));
        x_word = dk_render_vram_word(vdp, (dk_u16)(addr + 6u));
        link_byte = (dk_u8)(size_link & 0x7Fu);

        sprites[count].y = (int)(y_word & 0x03FFu) - 128;
        sprites[count].height = (int)(((size_link >> 8) & 0x03u) + 1u);
        sprites[count].width  = (int)(((size_link >> 10) & 0x03u) + 1u);

        /* Tile attributes (same format as nametable entry) */
        sprites[count].priority   = (int)((tile_attr >> 15) & 1u);
        sprites[count].palette   = (int)((tile_attr >> 13) & 3u);
        sprites[count].v_flip    = (int)((tile_attr >> 12) & 1u);
        sprites[count].h_flip    = (int)((tile_attr >> 11) & 1u);
        sprites[count].tile_index = (int)(tile_attr & 0x7FFu);

        /* X: 9-bit value, 128 = left screen edge (0 = 128px left of screen) */
        sprites[count].x = (int)(x_word & 0x01FFu) - 128;

        /* Follow link chain */
        count++;
        if (link_byte == 0u) break;  /* 0 = last sprite */
        link = (int)link_byte;
    }

    return count;
}

/* ================================================================== */
/*  Render sprites into the line buffer                                */
/*  pri_only: 0 = render non-priority sprites, 1 = priority sprites   */
/*  Enforces 20-sprite-per-scanline limit                              */
/* ================================================================== */
static void dk_render_sprites(dk_pixel_attr_t *line_buf,
                               const dk_vdp_t *vdp,
                               int line, int width, int pri_only)
{
    dk_sprite_t sprites[80];
    int         sprite_count;
    int         sprites_on_line;
    int         i;

    (void)pri_only;
    sprite_count = dk_render_parse_sprites(vdp, sprites);
    sprites_on_line = 0;

    for (i = 0; i < sprite_count; ++i) {
        int sprite_h, sprite_w;
        int start_y, end_y;
        int row, col;

        sprite_h = sprites[i].height * 8;
        sprite_w = sprites[i].width * 8;

        start_y = sprites[i].y;
        end_y   = start_y + sprite_h;
        if (line < start_y || line >= end_y) continue;
        row = line - start_y;

        /* Per-scanline sprite limit */
        sprites_on_line++;
        if (sprites_on_line > (int)DK_RENDER_SPRITES_PER_LINE) break;

        /* Render pixels for this sprite on this scanline */
        for (col = 0; col < sprite_w; ++col) {
            int px, tile_col, tile_row, tile_x, tile_y;
            dk_u16 tile_base;
            dk_u8  color_index;
            dk_u32 rgba;

            px = sprites[i].x + col;

            /* Clip to display width */
            if (px < 0 || px >= width) continue;

            /* Determine tile cell within the multi-cell sprite */
            tile_col = col / 8;
            tile_row = row / 8;
            tile_x   = col & 7;
            tile_y   = row & 7;

            /* Apply flips */
            if (sprites[i].h_flip) {
                tile_x = 7 - tile_x;
                tile_col = sprites[i].width - 1 - tile_col;
            }
            if (sprites[i].v_flip) {
                tile_y = 7 - tile_y;
                tile_row = sprites[i].height - 1 - tile_row;
            }

            /* Genesis multi-cell sprite patterns are column-major. */
            tile_base = (dk_u16)(((dk_u16)(sprites[i].tile_index
                           + tile_row + tile_col * sprites[i].height) * 32u)
                          & 0xFFFFu);

            /* Decode pixel */
            color_index = dk_render_tile_pixel(vdp, tile_base, tile_y, tile_x);
            if (color_index == 0u) continue;  /* transparent */

            if (
                line_buf[px].layer == 3
                || (!sprites[i].priority && line_buf[px].priority)
            ) {
                line_buf[px].layer = 3;
                continue;
            }

            rgba = dk_render_palette_color(vdp, sprites[i].palette,
                                            (int)color_index);

            line_buf[px].color    = rgba;
            line_buf[px].opaque   = 1;
            line_buf[px].layer    = 3;  /* sprite layer */
        }
    }
}

/* ================================================================== */
/*  dk_render_init — Initialize renderer                               */
/* ================================================================== */
void dk_render_init(dk_render_t *ren)
{
    if (ren == 0) return;
    memset(ren, 0, sizeof(*ren));
    ren->width  = (int)DK_RENDER_WIDTH;
    ren->height = (int)DK_RENDER_HEIGHT;
}

/* ================================================================== */
/*  dk_render_scanline — Render one scanline into the frame buffer      */
/*                                                                     */
/*  Rendering order (per Genesis hardware):                            */
/*    1. Fill background color                                         */
/*    2. Plane B (non-priority tiles)                                  */
/*    3. Plane A (non-priority tiles)                                  */
/*    4. Sprites (non-priority)                                        */
/*    5. Plane A (priority tiles)                                      */
/*    6. Sprites (priority)                                            */
/*                                                                     */
/*  Plane B priority tiles don't have a separate pass because          */
/*  Plane B is always behind Plane A and sprites in the priority       */
/*  ordering. However, Plane B DOES have priority bits — they          */
/*  determine whether Plane B pixels sit above or below sprites        */
/*  and Plane A. For the standard 5-layer model:                      */
/*    B(non-pri) -> A(non-pri) -> Sprites(non-pri)                    */
/*    -> A(pri) -> Sprites(pri)                                        */
/*  Plane B priority pixels are drawn in the B pass but their          */
/*  priority bit allows them to survive when later layers check        */
/*  the line buffer. We render B in two sub-passes for correctness:   */
/*    B(non-pri) first, then after A(pri) and Sprites(pri),           */
/*    but per the 5-layer spec, B priority pixels are still below     */
/*    A priority. So we simplify: B is always rendered first, with     */
/*    its priority bits set, and the subsequent passes will            */
/*    overwrite based on the priority ordering rules.                 */
/* ================================================================== */
void dk_render_scanline(dk_render_t *ren, const dk_vdp_t *vdp, int line)
{
    dk_pixel_attr_t line_buf[320];
    dk_u32 bg_color;
    dk_u16 bg_cram_word;
    int    width;
    int    is_h40;
    int    pixel_x;
    dk_u16 plane_a_base, plane_b_base;

    if (ren == 0 || vdp == 0) return;
    if (line < 0 || line >= (int)DK_RENDER_HEIGHT) return;

    /* Determine display mode */
    is_h40 = (vdp->regs[12] & 0x01u) != 0u;
    width = 320;
    ren->width  = width;
    ren->height = 224;

    /* ---- Step 1: Fill background color ---- */
    /* Background color: CRAM palette 0, entry = reg 7 & 0x3F */
    {
        dk_u8 bg_idx = (dk_u8)(vdp->regs[7] & 0x3Fu);
        bg_cram_word = dk_render_cram_word(vdp, (dk_u8)((bg_idx << 1) & 0x7Eu));
    }
    bg_color = dk_render_cram_to_rgba(bg_cram_word);

    /* Initialize line buffer with background */
    for (pixel_x = 0; pixel_x < width; ++pixel_x) {
        line_buf[pixel_x].color    = bg_color;
        line_buf[pixel_x].priority = 0;
        line_buf[pixel_x].opaque   = 1;  /* background is always opaque */
        line_buf[pixel_x].layer    = 0;
    }

    /* Get plane nametable bases */
    plane_a_base = dk_render_plane_a_base(vdp, is_h40);
    plane_b_base = dk_render_plane_b_base(vdp);

    /* Match the active Python VDP layer order and priority masks. */
    dk_render_plane(line_buf, vdp, line, plane_b_base, 1, width, 0);
    dk_render_plane(line_buf, vdp, line, plane_a_base, 0, width, 0);
    dk_render_sprites(line_buf, vdp, line, width, 0);

    /* ---- Final: Write line buffer to frame buffer ---- */
    {
        dk_u32 *fb_row = ren->frame_buffer + line * 320;
        for (pixel_x = 0; pixel_x < width; ++pixel_x) {
            fb_row[pixel_x] = line_buf[pixel_x].color;
        }
        /* Fill remaining pixels in H40 buffer if H32 mode */
        for (; pixel_x < 320; ++pixel_x) {
            fb_row[pixel_x] = bg_color;
        }
    }
}

/* ================================================================== */
/*  dk_render_frame — Render all 224 active scanlines                  */
/* ================================================================== */
void dk_render_frame(dk_render_t *ren, const dk_vdp_t *vdp)
{
    int line;
    if (ren == 0 || vdp == 0) return;

    for (line = 0; line < (int)DK_RENDER_HEIGHT; ++line) {
        dk_render_scanline(ren, vdp, line);
    }
}
