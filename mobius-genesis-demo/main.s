/*
 * Möbius Topology Demo - Sega Genesis / Mega Drive, Motorola 68000.
 *
 * A deliberately modest, real-hardware first renderer. Thirty-two paired
 * edge samples, eight cross-section samples, and one tracer make a 73-sprite
 * ribbon: safely within the Genesis H40 80-sprite table.
 */
        .cpu 68000
        .equ VDP_DATA,       0xC00000
        .equ VDP_CTRL,       0xC00004
        .equ PAD1_DATA,      0xA10003
        .equ PAD1_CTRL,      0xA10009
        .equ TMSS,           0xA14000
        /* 68000 stacks grow downward: start at the top, not the bottom, of
         * the Genesis 64 KiB work RAM ($FF0000-$FFFFFF). */
        .equ STACK_TOP,      0x00FFFF00
        .equ RAM,            0x00FF0000
        .equ SpriteBuf,      0x00FF0800
        .equ MAX_SPRITES,    80
        .equ VBlankFlag,     RAM+0
        .equ Buttons,        RAM+2
        .equ OldButtons,     RAM+4
        .equ NewButtons,     RAM+6
        .equ Mode,           RAM+8       /* 0 normal, 1 Möbius */
        .equ AutoRotate,     RAM+10
        .equ TraceRun,       RAM+12
        .equ CamX,           RAM+14
        .equ CamY,           RAM+16
        .equ Twist,          RAM+18      /* 0, 128=180deg, 256=360deg */
        .equ TracePos,       RAM+20      /* 0..511: two logical laps */
        .equ TempSU,         RAM+24
        .equ TempCU,         RAM+26
        .equ TempHS,         RAM+28
        .equ TempHC,         RAM+30
        .equ TempX,          RAM+32
        .equ TempY,          RAM+34
        .equ TempZ,          RAM+36
        .equ CosX,           RAM+38
        .equ SinX,           RAM+40
        .equ CosY,           RAM+42
        .equ SinY,           RAM+44
        .equ SpriteCount,    RAM+46
        .equ TempOX,          RAM+48
        .equ TempOY,          RAM+50

        .section .vectors,"a"
        .long STACK_TOP
        .long Reset
        .rept 28
        .long Fault
        .endr
        .long VInt                       /* vector $78: level-6 V blank */
        .rept 33
        .long Fault
        .endr

        .section .header,"a"
        .ascii "SEGA MEGA DRIVE "
        .ascii "(C)2026 OPENAI  "
        .ascii "MOBIUS TOPOLOGY DEMO"
        .space 28                         /* domestic title: 48 bytes */
        .ascii "MOBIUS TOPOLOGY DEMO"
        .space 28                         /* overseas title: 48 bytes */
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
        move.w #0x2700,%sr
        lea STACK_TOP,%sp
        move.l #0x53454741,TMSS           /* TMSS unlock on later consoles */
        lea RAM,%a0
        move.w #0x7ff,%d0
.clear_ram:
        clr.l (%a0)+
        dbra %d0,.clear_ram
        lea VDP_CTRL,%a6
        /* 320x224, Plane A=$C000, Plane B=$E000, SAT=$D800, auto
         * increment=2.  The Window table is in otherwise-unused, cleared
         * VRAM at $F000 -- never at $0000, where the tile patterns live. */
        move.w #0x8004,(%a6)
        /* Keep display and V interrupt disabled while VRAM/CRAM are being
         * initialized.  This avoids exposing the VDP's power-on palette. */
        move.w #0x8104,(%a6)
        move.w #0x8230,(%a6)
        move.w #0x833C,(%a6)              /* Window name table = $F000 */
        move.w #0x8407,(%a6)
        move.w #0x856C,(%a6)
        move.w #0x8700,(%a6)
        move.w #0x8A00,(%a6)
        move.w #0x8B00,(%a6)
        move.w #0x8C81,(%a6)
        move.w #0x8D3F,(%a6)
        move.w #0x8F02,(%a6)
        move.w #0x9001,(%a6)              /* 64x32 cell planes */
        /* Bit 7 selects the right/bottom side.  Without it, positions 31
         * select the left/top side and the Window masks nearly all of Plane A
         * and the sprite field. */
        move.w #0x919F,(%a6)              /* Window starts off right edge */
        move.w #0x929F,(%a6)              /* and below the visible screen */
        bsr ClearVDP
        bsr UploadTiles
        bsr UploadPalette
        bsr DrawStaticText
        bsr ResetState
        /* Explicitly set both vertical-scroll entries.  This avoids relying
         * on the VDP's power-on VSRAM state. */
        move.l #0x40000010,(%a6)
        move.w #-4,VDP_DATA
        move.w #0,VDP_DATA
        move.w #0x8174,(%a6)              /* display + V interrupt */
        move.w #0x2300,%sr

MainLoop:
        bsr WaitVBlank
        bsr ReadPad
        bsr UpdateControls
        bsr MakeCameraTable
        bsr BuildSprites
        /* A long VDP command is two control-port words.  Do not let VInt's
         * status-port acknowledgement split a command or a transfer. */
        move.w %sr,-(%sp)
        move.w #0x2700,%sr
        bsr UploadSprites
        bsr DrawDynamicText
        move.w (%sp)+,%sr
        bra.s MainLoop

/* V blank writes are synchronized by this tiny interrupt handshake. */
VInt:
        move.w VDP_CTRL,%d0               /* acknowledge VDP interrupt */
        move.b #1,VBlankFlag
        rte
Fault:
        stop #0x2700
        bra.s Fault
WaitVBlank:
.wait:  tst.b VBlankFlag
        beq.s .wait
        clr.b VBlankFlag
        rts

ClearVDP:
        move.l #0x40000000,(%a6)
        move.w #0x7fff,%d0
.vram:  move.w #0,VDP_DATA
        dbra %d0,.vram
        move.l #0xC0000000,(%a6)
        moveq #31,%d0
.cram:  move.w #0,VDP_DATA
        dbra %d0,.cram
        move.l #0x40000010,(%a6)
        moveq #39,%d0
.vsram: move.w #0,VDP_DATA
        dbra %d0,.vsram
        rts

UploadTiles:
        lea TileArt,%a0
        move.l #0x40000000,VDP_CTRL
        move.w #(TileArtEnd-TileArt)/2-1,%d0
UploadTilesLoop:  move.w (%a0)+,VDP_DATA
        dbra %d0,UploadTilesLoop
        rts
UploadPalette:
        lea Palette,%a0
        move.l #0xC0000000,VDP_CTRL
        moveq #63,%d0
UploadPaletteLoop:  move.w (%a0)+,VDP_DATA
        dbra %d0,UploadPaletteLoop
        rts

/* Standard 3-button Genesis protocol. Bits: UDLRBCAS in d0. */
ReadPad:
        move.b #0x40,PAD1_CTRL
        move.b #0x40,PAD1_DATA
        moveq #0,%d0
        move.b PAD1_DATA,%d0
        andi.w #0x003f,%d0
        move.b #0,PAD1_DATA
        moveq #0,%d1
        move.b PAD1_DATA,%d1
        andi.w #0x0030,%d1
        lsl.w #2,%d1
        or.w %d1,%d0
        not.w %d0
        andi.w #0x00ff,%d0
        move.w Buttons,OldButtons
        move.w %d0,Buttons
        move.w OldButtons,%d1
        not.w %d1
        and.w %d1,%d0
        move.w %d0,NewButtons
        rts

ResetState:
        move.w #1,Mode
        move.w #1,AutoRotate
        move.w #1,TraceRun
        move.w #16,CamX
        move.w #24,CamY
        move.w #128,Twist
        clr.w TracePos
        rts

UpdateControls:
        move.w NewButtons,%d0
        btst #6,%d0                       /* A */
        beq.s .no_a
        eori.w #1,Mode
        tst.w Mode
        bne.s .no_a
        clr.w Twist
.no_a: btst #4,%d0                       /* B */
        beq.s .no_b
        eori.w #1,AutoRotate
.no_b: btst #5,%d0                       /* C */
        beq.s .no_c
        eori.w #1,TraceRun
.no_c: btst #7,%d0                       /* Start */
        beq.s .dirs
        bsr ResetState
.dirs: move.w Buttons,%d0
        btst #5,%d0                       /* C held: manual twist */
        beq.s .camera
        tst.w Mode                         /* manual twist applies to ribbon mode */
        beq.s .trace
        btst #2,%d0
        beq.s .tw_right
        subq.w #4,Twist
        bpl.s .tw_right
        clr.w Twist
.tw_right:
        btst #3,%d0
        beq.s .trace
        addq.w #4,Twist
        cmpi.w #256,Twist
        ble.s .trace
        move.w #256,Twist
        bra.s .trace
.camera:
        btst #2,%d0
        beq.s .uc_right
        subq.w #2,CamY
.uc_right: btst #3,%d0
        beq.s .up
        addq.w #2,CamY
.up:   btst #0,%d0
        beq.s .down
        subq.w #2,CamX
.down: btst #1,%d0
        beq.s .auto
        addq.w #2,CamX
.auto: tst.w AutoRotate
        beq.s .trace
        addq.w #1,CamY
.trace:
        tst.w TraceRun
        beq.s .done
        addq.w #2,TracePos
        andi.w #511,TracePos
.done: rts

/* Sine table values have a magnitude of 127, ideal for signed 8.8-ish math. */
Sin:
        andi.w #255,%d0
        move.w %d0,%d1
        lsr.w #6,%d1
        andi.w #63,%d0
        lea QuarterSine,%a0
        cmpi.w #1,%d1
        beq.s .q1
        cmpi.w #2,%d1
        beq.s .q2
        cmpi.w #3,%d1
        beq.s .q3
        moveq #0,%d2
        move.b (%a0,%d0.w),%d2
        move.w %d2,%d0
        rts
.q1:   moveq #64,%d2
        sub.w %d0,%d2
        moveq #0,%d0
        move.b (%a0,%d2.w),%d0
        rts
.q2:   moveq #0,%d2
        move.b (%a0,%d0.w),%d2
        neg.w %d2
        move.w %d2,%d0
        rts
.q3:   moveq #64,%d2
        sub.w %d0,%d2
        moveq #0,%d0
        move.b (%a0,%d2.w),%d0
        neg.w %d0
        rts

MakeCameraTable:
        move.w CamX,%d0
        bsr Sin
        move.w %d0,SinX
        move.w CamX,%d0
        addi.w #64,%d0
        bsr Sin
        move.w %d0,CosX
        move.w CamY,%d0
        bsr Sin
        move.w %d0,SinY
        move.w CamY,%d0
        addi.w #64,%d0
        bsr Sin
        move.w %d0,CosY
        rts

/* Input d0=u (0..255), d1=v (-12..12), d3=VDP tile attribute.
 * Parametric model: x=(R+v*cos(t))*cos(u), y=(R+v*cos(t))*sin(u),
 * z=v*sin(t), with t = u*twist/256. Normal mode forces t=0; integer
 * divisions use >>7.
 */
BuildVertex:
        move.w %d0,%d4
        bsr Sin
        move.w %d0,TempSU
        move.w %d4,%d0
        addi.w #64,%d0
        bsr Sin
        move.w %d0,TempCU
        move.w %d4,%d0
        move.w Twist,%d2
        tst.w Mode
        bne.s .twist_1
        clr.w %d2
.twist_1:
        muls %d2,%d0
        asr.l #8,%d0
        bsr Sin
        move.w %d0,TempHS
        move.w %d4,%d0
        move.w Twist,%d2
        tst.w Mode
        bne.s .twist_2
        clr.w %d2
.twist_2:
        muls %d2,%d0
        asr.l #8,%d0
        addi.w #64,%d0
        bsr Sin
        move.w %d0,TempHC
        /* Radius, then the three unrotated coordinates. */
        move.w %d1,%d0
        muls TempHC,%d0
        asr.l #7,%d0
        addi.w #72,%d0
        muls TempCU,%d0
        asr.l #7,%d0
        move.w %d0,TempX
        move.w %d0,TempOX
        move.w %d1,%d0
        move.w TempHC,%d2
        muls %d2,%d0
        asr.l #7,%d0
        addi.w #72,%d0
        muls TempSU,%d0
        asr.l #7,%d0
        move.w %d0,TempY
        move.w %d1,%d0
        muls TempHS,%d0
        asr.l #7,%d0
        move.w %d0,TempZ
        /* Y rotation. */
        move.w TempX,%d0
        muls CosY,%d0
        move.w TempZ,%d2
        muls SinY,%d2
        add.l %d2,%d0
        asr.l #7,%d0
        move.w %d0,TempX
        /* z1 = -oldx*sinY + oldz*cosY. */
        move.w TempOX,%d0
        muls SinY,%d0
        neg.l %d0
        move.w TempZ,%d2
        muls CosY,%d2
        add.l %d2,%d0
        asr.l #7,%d0
        move.w %d0,TempZ
        /* X rotation and perspective. */
        move.w TempY,%d0
        move.w %d0,TempOY
        muls CosX,%d0
        move.w TempZ,%d2
        muls SinX,%d2
        sub.l %d2,%d0
        asr.l #7,%d0
        move.w %d0,TempY
        move.w TempOY,%d0
        muls SinX,%d0
        move.w TempZ,%d2
        muls CosX,%d2
        add.l %d2,%d0
        asr.l #7,%d0
        addi.w #280,%d0
        move.w %d0,%d4                   /* positive camera distance */
        move.w TempX,%d0
        muls #150,%d0
        divs %d4,%d0
        addi.w #160,%d0
        move.w TempY,%d1
        muls #150,%d1
        divs %d4,%d1
        neg.w %d1
        addi.w #112,%d1
        bsr AddSprite
        rts

/* One 1x1 sprite; d0/d1 are screen coordinates, d3 selects tile/palette. */
AddSprite:
        cmpi.w #MAX_SPRITES,SpriteCount
        bge.s .full
        lea SpriteBuf,%a0
        move.w SpriteCount,%d2
        lsl.w #3,%d2
        adda.w %d2,%a0
        addi.w #128,%d1
        move.w %d1,(%a0)+
        clr.b (%a0)+
        move.w SpriteCount,%d2
        addq.b #1,%d2
        move.b %d2,(%a0)+
        move.w %d3,(%a0)+
        addi.w #128,%d0
        move.w %d0,(%a0)+
        addq.w #1,SpriteCount
.full:
        rts

BuildSprites:
        clr.w SpriteCount
        moveq #0,%d6
        moveq #31,%d7
.sample:
        /* Bright/dark edge assignment flips at the half-twist in Möbius. */
        move.w #0x0001,%d3
        tst.w Mode
        beq.s .colors
        btst #7,%d6
        beq.s .colors
        move.w #0x2001,%d3
.colors:
        move.w %d6,%d0
        move.w #-12,%d1
        bsr BuildVertex
        move.w #0x2001,%d3
        tst.w Mode
        beq.s .bs_right
        btst #7,%d6
        beq.s .bs_right
        move.w #0x0001,%d3
.bs_right:
        move.w %d6,%d0
        move.w #12,%d1
        bsr BuildVertex
        /* Every fourth segment gets a centre reference dot. */
        move.w %d6,%d2
        andi.w #31,%d2
        bne.s .no_cross
        move.w #0x0001,%d3
        move.w %d6,%d0
        clr.w %d1
        bsr BuildVertex
.no_cross:
        addi.w #8,%d6
        dbra %d7,.sample
        /* Tracer's local offset reverses after the first Möbius lap. */
        move.w TracePos,%d0
        andi.w #255,%d0
        move.w #6,%d1
        tst.w Mode
        beq.s .tracer
        btst #8,TracePos
        beq.s .tracer
        neg.w %d1
.tracer:
        move.w #0x4001,%d3
        bsr BuildVertex
        lea SpriteBuf,%a0
        move.w SpriteCount,%d0
        beq.s BuildSpritesDone
        subq.w #1,%d0
        lsl.w #3,%d0
        clr.b 3(%a0,%d0.w)                /* last SAT link terminates list */
BuildSpritesDone:
        rts

UploadSprites:
        lea SpriteBuf,%a0
        move.l #0x58000003,VDP_CTRL       /* VRAM write $D800 */
        move.w SpriteCount,%d0
        beq.s UploadSpritesDone
        lsl.w #2,%d0                      /* 8 bytes/sprite = 4 words */
        subq.w #1,%d0
.copy:  move.w (%a0)+,VDP_DATA
        dbra %d0,.copy
UploadSpritesDone:
        rts

/* d0 = VRAM byte address, a0 = zero-terminated upper-case text. */
WriteText:
        move.w %d0,%d1
        andi.l #0x3fff,%d1
        swap %d1
        move.w %d0,%d2
        lsr.w #8,%d2
        lsr.w #6,%d2
        andi.w #3,%d2
        or.l %d2,%d1
        ori.l #0x40000000,%d1
        move.l %d1,VDP_CTRL
.next:  moveq #0,%d1
        move.b (%a0)+,%d1
        beq.s .end
        bsr CharTile
        ori.w #0x6000,%d1                 /* palette 3, text colour */
        move.w %d1,VDP_DATA
        bra.s .next
.end:   rts
CharTile:
        cmpi.b #' ',%d1
        beq.s .blank
        cmpi.b #'A',%d1
        blt.s .punct
        cmpi.b #'Y',%d1
        bgt.s .digit
        subi.b #'A',%d1
        lea LetterMap,%a1
        moveq #0,%d2
        move.b (%a1,%d1.w),%d2
        move.w %d2,%d1
        rts
.digit: cmpi.b #'0',%d1
        blt.s .punct
        cmpi.b #'9',%d1
        bgt.s .punct
        subi.b #'0',%d1
        lea DigitMap,%a1
        moveq #0,%d2
        move.b (%a1,%d1.w),%d2
        move.w %d2,%d1
        rts
.punct: cmpi.b #':',%d1
        bne.s .slash
        moveq #26,%d1
        rts
.slash: cmpi.b #'/',%d1
        bne.s .blank
        moveq #27,%d1
        rts
.blank: clr.w %d1
        rts

DrawStaticText:
        lea TxtControls,%a0
        move.w #(0xC000+(26*64+1)*2),%d0
        bsr WriteText
        rts
DrawDynamicText:
        tst.w Mode
        beq.s .normal
        lea TxtMobius,%a0
        bra.s .title
.normal:lea TxtNormal,%a0
.title: move.w #(0xC000+(1*64+1)*2),%d0
        bsr WriteText
        tst.w Mode
        beq.s .normal_info
        lea TxtMobiusInfo,%a0
        bra.s .info_out
.normal_info:
        lea TxtNormalInfo,%a0
.info_out:
        move.w #(0xC000+(23*64+1)*2),%d0
        bsr WriteText
        lea TxtTwist,%a0
        move.w #(0xC000+(3*64+1)*2),%d0
        bsr WriteText
        move.w Twist,%d0
        cmpi.w #64,%d0
        blt.s .tw0
        cmpi.w #192,%d0
        blt.s .tw180
        lea Txt360,%a0
        bra.s .twout
.tw0:   lea Txt000,%a0
        bra.s .twout
.tw180: lea Txt180,%a0
.twout: move.w #(0xC000+(3*64+8)*2),%d0
        bsr WriteText
        lea TxtLap,%a0
        move.w #(0xC000+(5*64+1)*2),%d0
        bsr WriteText
        move.w TracePos,%d0
        lsr.w #8,%d0
        beq.s .lap0
        lea Txt1,%a0
        bra.s .lapout
.lap0:  lea Txt0,%a0
.lapout:move.w #(0xC000+(5*64+6)*2),%d0
        bsr WriteText
        rts

        .balign 2
TxtMobius:     .asciz "MOBIUS STRIP"
TxtNormal:     .asciz "NORMAL RING "
TxtTwist:      .asciz "TWIST: "
Txt000:        .asciz "000"
Txt180:        .asciz "180"
Txt360:        .asciz "360"
TxtLap:        .asciz "LAP: "
Txt0:          .asciz "0"
Txt1:          .asciz "1"
TxtNormalInfo: .asciz "NORMAL  2 SIDES / 2 EDGES"
TxtMobiusInfo: .asciz "MOBIUS  1 SIDE / 1 EDGE  "
TxtControls:   .asciz "A MODE  B AUTO  C TRACE  START RESET"
LetterMap: .byte 2,3,4,5,6,7,8,9,10,0,0,11,12,13,14,15,0,16,17,18,19,0,20,0,21
DigitMap:  .byte 22,23,24,0,0,0,0,0,25,0
QuarterSine:
        .byte 0,3,6,9,12,16,19,22,25,28,31,34,37,40,43,46
        .byte 49,51,54,57,60,63,65,68,71,73,76,78,81,83,85,88
        .byte 90,92,94,96,98,100,102,104,106,107,109,111,112,113,115,116
        .byte 117,118,120,121,122,122,123,124,125,125,126,126,126,127,127,127,127

/* Tile 0 blank; tile 1 is a palette-1 dot; tiles 2..27 use palette index 1.
 * A Genesis tile row is eight 4-bit pixels, so each source bit must occupy a
 * nibble (for example, 00111000 rather than the byte value $38). */
        .balign 2
TileArt:
        .rept 8
        .long 0
        .endr
        .rept 8
        .long 0x01111110
        .endr
/* Compact 5x7 glyphs, plane 0 only. A B C D E F G H I L M N O P R S T U W Y */
GlyphData:
        .long 0x00111000,0x01000100,0x01000100,0x01111100,0x01000100,0x01000100,0x01000100,0
        .long 0x01111000,0x01000100,0x01000100,0x01111000,0x01000100,0x01000100,0x01111000,0
        .long 0x00111100,0x01000000,0x01000000,0x01000000,0x01000000,0x01000000,0x00111100,0
        .long 0x01111000,0x01000100,0x01000100,0x01000100,0x01000100,0x01000100,0x01111000,0
        .long 0x01111100,0x01000000,0x01000000,0x01111000,0x01000000,0x01000000,0x01111100,0
        .long 0x01111100,0x01000000,0x01000000,0x01111000,0x01000000,0x01000000,0x01000000,0
        .long 0x00111100,0x01000000,0x01000000,0x01011100,0x01000100,0x01000100,0x00111100,0
        .long 0x01000100,0x01000100,0x01000100,0x01111100,0x01000100,0x01000100,0x01000100,0
        .long 0x01111100,0x00010000,0x00010000,0x00010000,0x00010000,0x00010000,0x01111100,0
        .long 0x01000000,0x01000000,0x01000000,0x01000000,0x01000000,0x01000000,0x01111100,0
        .long 0x01000100,0x01101100,0x01010100,0x01010100,0x01000100,0x01000100,0x01000100,0
        .long 0x01000100,0x01100100,0x01010100,0x01001100,0x01000100,0x01000100,0x01000100,0
        .long 0x00111000,0x01000100,0x01000100,0x01000100,0x01000100,0x01000100,0x00111000,0
        .long 0x01111000,0x01000100,0x01000100,0x01111000,0x01000000,0x01000000,0x01000000,0
        .long 0x01111000,0x01000100,0x01000100,0x01111000,0x01010000,0x01001000,0x01000100,0
        .long 0x00111100,0x01000000,0x01000000,0x00111000,0x00000100,0x00000100,0x01111000,0
        .long 0x01111100,0x00010000,0x00010000,0x00010000,0x00010000,0x00010000,0x00010000,0
        .long 0x01000100,0x01000100,0x01000100,0x01000100,0x01000100,0x01000100,0x00111000,0
        .long 0x01000100,0x01000100,0x01000100,0x01010100,0x01010100,0x01010100,0x00101000,0
        .long 0x01000100,0x01000100,0x00101000,0x00010000,0x00010000,0x00010000,0x00010000,0
/* 0,1,2,8,:,/ */
        .long 0x00111000,0x01000100,0x01001100,0x01010100,0x01100100,0x01000100,0x00111000,0
        .long 0x00010000,0x00110000,0x00010000,0x00010000,0x00010000,0x00010000,0x00111000,0
        .long 0x00111000,0x01000100,0x00000100,0x00001000,0x00010000,0x00100000,0x01111100,0
        .long 0x00111000,0x01000100,0x01000100,0x00111000,0x01000100,0x01000100,0x00111000,0
        .long 0,0x00010000,0x00010000,0,0x00010000,0x00010000,0,0
        .long 0x00000100,0x00001000,0x00010000,0x00100000,0x01000000,0,0,0
TileArtEnd:
Palette:
        .word 0x0000,0x00E0,0x0080,0x0000,0,0,0,0,0,0,0,0,0,0,0,0
        .word 0x0000,0x0020,0x0008,0,0,0,0,0,0,0,0,0,0,0,0,0
        .word 0x0000,0x0EEE,0x00E,0,0,0,0,0,0,0,0,0,0,0,0,0
        .word 0x0000,0x0EEE,0x0888,0,0,0,0,0,0,0,0,0,0,0,0,0
RomEnd:
