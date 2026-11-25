/*
 * Deterministic VDP/SAT diagnostic for the Möbius demo.
 *
 * Assemble with --defsym DIAG_SPRITE_COUNT=N, where N is one of
 * 0, 1, 4, 8, 16, 32, 64, or 73.  This file deliberately contains no
 * geometry, projection, controller, dynamic-text, or runtime VDP traffic.
 */
        .cpu 68000

        .equ VDP_DATA,       0xC00000
        .equ VDP_CTRL,       0xC00004
        .equ HV_COUNTER,     0xC00008
        .equ PSG_PORT,       0xC00011
        .equ TMSS,           0xA14000
        .equ STACK_TOP,      0x00FFFF00
        .equ RAM,            0x00FF0000
        .equ VBlankFlag,     RAM+0
        .equ SpriteCount,    RAM+2
        .equ SpriteBuf,      0x00FF0800
        .equ SAT_VRAM,       0xD800
        .equ SPRITE_CAPACITY,80

        .ifndef DIAG_SPRITE_COUNT
        .equ DIAG_SPRITE_COUNT, 0
        .endif
        .if DIAG_SPRITE_COUNT > SPRITE_CAPACITY
        .error "DIAG_SPRITE_COUNT exceeds the H40 SAT capacity"
        .endif

        .section .vectors,"a"
        .long STACK_TOP
        .long Reset
        .rept 28
        .long Fault
        .endr
        .long VInt
        .rept 33
        .long Fault
        .endr

        .section .header,"a"
        .ascii "SEGA MEGA DRIVE "
        .ascii "(C)2026 OPENAI  "
        .ascii "MOBIUS VDP/SAT TEST"
        .space 29
        .ascii "MOBIUS VDP/SAT TEST"
        .space 29
        .ascii "GM 00000000-00"
        .word 0
        .ascii "J               "
        .long 0
        .long RomEnd-1
        .long 0x00FF0000
        .long 0x00FFFFFF
        .ascii "            "
        .ascii "            "
        .ascii "                                        "
        .ascii "JUE             "

        .section .text,"ax"
        .globl Reset
Reset:
        move.w #0x2700,%sr                 /* no interrupt can split a command */
        lea STACK_TOP,%sp
        move.l #0x53454741,TMSS

        lea RAM,%a0
        move.w #0x7ff,%d0
.clear_ram:
        clr.l (%a0)+
        dbra %d0,.clear_ram

        /* Fixed, documented map: tiles 0000-019F, hscroll B000-B3FF,
         * Plane A C000-CFFF, SAT D800-DA7F, Plane B E000-EFFF,
         * Window F000-FFFF.  Display stays off until setup is complete. */
        move.w #0x8004,VDP_CTRL
        move.w #0x8104,VDP_CTRL
        move.w #0x8230,VDP_CTRL             /* Plane A: C000 */
        move.w #0x833C,VDP_CTRL             /* Window:  F000 */
        move.w #0x8407,VDP_CTRL             /* Plane B: E000 */
        move.w #0x856C,VDP_CTRL             /* SAT:     D800 */
        move.w #0x8700,VDP_CTRL             /* black backdrop */
        move.w #0x8A00,VDP_CTRL
        move.w #0x8B00,VDP_CTRL             /* full-screen H scroll */
        move.w #0x8C81,VDP_CTRL             /* H40 */
        move.w #0x8D2C,VDP_CTRL             /* H scroll: B000 */
        move.w #0x8F02,VDP_CTRL             /* auto increment: 2 */
        move.w #0x9001,VDP_CTRL             /* 64 x 32 cells */
        /* Bit 7 selects the right/bottom side of the boundary.  Without
         * it, position $1F covers the left/top of Plane A with blank Window. */
        move.w #0x919F,VDP_CTRL             /* Window is to the right */
        move.w #0x929F,VDP_CTRL             /* Window is below */

        bsr ClearVDP
        bsr UploadFont
        bsr UploadPalette
        bsr DrawStaticText
        bsr BuildDiagnosticSAT
        bsr UploadSAT

        move.w #0x8174,VDP_CTRL             /* display + V interrupt */
        move.w #0x2300,%sr
.main_wait:
        bsr WaitVBlank
        bra.s .main_wait

/* The sole VDP read in this ROM is the legal status acknowledge at C00004.
 * There is no read of VDP_DATA (C00000). */
VInt:
        move.w VDP_CTRL,%d0
        move.b #1,VBlankFlag
        rte

Fault:
        move.w #0x8703,VDP_CTRL             /* red backdrop means guard fault */
        stop #0x2700
        bra.s Fault

WaitVBlank:
.poll:  tst.b VBlankFlag
        beq.s .poll
        clr.b VBlankFlag
        rts

/* d0 = VRAM byte address.  Command = ((A & 3fff) << 16) |
 * ((A & c000) >> 14) | 40000000.  For SAT D800 this is 58000003. */
SetVRAMWrite:
        move.l %d0,%d1
        andi.l #0x3fff,%d1
        swap %d1
        andi.l #0x0000c000,%d0
        lsr.l #8,%d0
        lsr.l #6,%d0
        or.l %d0,%d1
        ori.l #0x40000000,%d1
        move.l %d1,VDP_CTRL
        rts

ClearVDP:
        moveq #0,%d0
        bsr SetVRAMWrite
        move.w #0x7fff,%d0
.clear_vram:  move.w #0,VDP_DATA
        dbra %d0,.clear_vram
        move.l #0xC0000000,VDP_CTRL
        moveq #31,%d0
.clear_cram:  move.w #0,VDP_DATA
        dbra %d0,.clear_cram
        move.l #0x40000010,VDP_CTRL
        moveq #39,%d0
.clear_vsram: move.w #0,VDP_DATA
        dbra %d0,.clear_vsram
        rts

UploadFont:
        lea DiagnosticFont,%a0
        moveq #0,%d0
        bsr SetVRAMWrite
        move.w #(DiagnosticFontEnd-DiagnosticFont)/2-1,%d0
.font_copy:  move.w (%a0)+,VDP_DATA
        dbra %d0,.font_copy
        rts

UploadPalette:
        lea DiagnosticPalette,%a0
        move.l #0xC0000000,VDP_CTRL
        moveq #63,%d0
.palette_copy:  move.w (%a0)+,VDP_DATA
        dbra %d0,.palette_copy
        rts

DrawStaticText:
        lea StaticTextTiles,%a0
        move.l #0x0000C618,%d0             /* Plane A, row 12, column 12 */
        bsr SetVRAMWrite
        moveq #StaticTextTilesEnd-StaticTextTiles-1,%d0
.text_copy:  move.b (%a0)+,%d1
        move.w %d1,VDP_DATA
        dbra %d0,.text_copy
        rts

/* Phase 1 writes no SpriteBuf data.  It writes precisely one terminated SAT
 * record directly to D800: Y=0, size/link=0, attr=0, X=0.  Other phases copy
 * exactly N prevalidated 8-byte records into SpriteBuf before upload. */
BuildDiagnosticSAT:
.if DIAG_SPRITE_COUNT == 0
        clr.w SpriteCount
.else
        lea DiagnosticSAT,%a0
        lea SpriteBuf,%a1
        move.w #(DIAG_SPRITE_COUNT*4)-1,%d0
.sat_fixture_copy:  move.w (%a0)+,(%a1)+
        dbra %d0,.sat_fixture_copy
        move.w #DIAG_SPRITE_COUNT,SpriteCount
.endif
        rts

UploadSAT:
        move.w SpriteCount,%d0
        beq.s .terminator
        cmpi.w #SPRITE_CAPACITY,%d0
        bhi Fault
        lea SpriteBuf,%a0
        move.l #SAT_VRAM,%d1
        move.l %d1,%d0
        bsr SetVRAMWrite
        move.w SpriteCount,%d0
        lsl.w #2,%d0                        /* N sprites x 4 words */
        subq.w #1,%d0                       /* DBRA executes count + 1 */
.sat_upload_copy:  move.w (%a0)+,VDP_DATA
        dbra %d0,.sat_upload_copy
        rts
.terminator:
        move.l #SAT_VRAM,%d0
        bsr SetVRAMWrite
        moveq #0,%d0
        move.w %d0,VDP_DATA
        move.w %d0,VDP_DATA
        move.w %d0,VDP_DATA
        move.w %d0,VDP_DATA
        rts

        .section .rodata,"a"
/* Pixel value 1 is white (palette 0, colour 1).  Tile 1 is the known dot
 * sprite; tiles 2-12 spell the static message. */
DiagnosticFont:
        .rept 8
        .long 0x00000000                    /* tile 0: blank */
        .endr
        .rept 8
        .long 0x01111110                    /* tile 1: dot */
        .endr
        /* M */ .long 0x11000011,0x11100111,0x11011011,0x11000011,0x11000011,0x11000011,0x11000011,0x00000000
        /* O */ .long 0x01111110,0x11000011,0x11000011,0x11000011,0x11000011,0x11000011,0x01111110,0x00000000
        /* B */ .long 0x11111110,0x11000011,0x11000011,0x11111110,0x11000011,0x11000011,0x11111110,0x00000000
        /* I */ .long 0x01111110,0x00011000,0x00011000,0x00011000,0x00011000,0x00011000,0x01111110,0x00000000
        /* U */ .long 0x11000011,0x11000011,0x11000011,0x11000011,0x11000011,0x11000011,0x01111110,0x00000000
        /* S */ .long 0x01111110,0x11000011,0x11000000,0x01111110,0x00000011,0x11000011,0x01111110,0x00000000
        /* V */ .long 0x11000011,0x11000011,0x11000011,0x11000011,0x11000011,0x01100110,0x00011000,0x00000000
        /* D */ .long 0x11111100,0x11000010,0x11000011,0x11000011,0x11000011,0x11000010,0x11111100,0x00000000
        /* P */ .long 0x11111110,0x11000011,0x11000011,0x11111110,0x11000000,0x11000000,0x11000000,0x00000000
        /* T */ .long 0x11111111,0x00011000,0x00011000,0x00011000,0x00011000,0x00011000,0x00011000,0x00000000
        /* E */ .long 0x11111111,0x11000000,0x11000000,0x11111110,0x11000000,0x11000000,0x11111111,0x00000000
DiagnosticFontEnd:

/* index 0 black, 1 white, 2 green, 3 red.  Red is only selected by Fault. */
DiagnosticPalette:
        .word 0x0000,0x0EEE,0x00E0,0x000E
        .rept 60
        .word 0x0000
        .endr

StaticTextTiles:
        .byte 2,3,4,5,6,7,0,8,9,10,0,11,12,7,11
StaticTextTilesEnd:

        .balign 2
DiagnosticSAT:
.if DIAG_SPRITE_COUNT > 0
        .set diagnostic_sprite,0
        .rept DIAG_SPRITE_COUNT
        .word 208+(diagnostic_sprite/10)*12 /* Y = 128 + 80 + row*12 */
        .if diagnostic_sprite == DIAG_SPRITE_COUNT-1
        .word 0                             /* final link */
        .else
        .word diagnostic_sprite+1
        .endif
        .word 1                             /* dot tile; palette 0 */
        /* A compact on-screen grid.  Sprite 0 is at X=128+150, Y=128+80;
         * later entries stay in range so depth is the only variable. */
        .word 278+(diagnostic_sprite-(diagnostic_sprite/10)*10)*14
        .set diagnostic_sprite,diagnostic_sprite+1
        .endr
.endif
DiagnosticSATEnd:

RomEnd:
