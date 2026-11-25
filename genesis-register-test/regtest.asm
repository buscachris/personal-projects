; ===========================================================================
; SEGA GENESIS - 68K REGISTER TEST
; Shows D0-D7/A0-A7 after ALU/memory tests. D-pad picks test, A/START runs.
; Built with asl + p2bin (same toolchain as the S1 disasm).
; ===========================================================================
        CPU     68000

; ---- hardware ----
VDP_DATA        EQU     $C00000
VDP_CTRL        EQU     $C00004
PSG             EQU     $C00011
PAD1            EQU     $A10003
VERSION_REG     EQU     $A10001

; ---- RAM (68k clear at boot) ----
REG_BUF         EQU     $FF0000        ; d0-d7 (32) + a0-a6 (28) + a7 (4)
PAD_BUF         EQU     $FF0040        ; previous pad state
TEST_BUF        EQU     $FF0044        ; current test number
VDP_BUF         EQU     $FF0048        ; console version (0-F)
PAD_STR         EQU     $FF0050        ; 9-char pad string

; ===========================================================================
; VECTOR TABLE (at $000000)
; ===========================================================================
        ORG     0
        dc.l    $00FFE000               ; initial SSP
        dc.l    Entry                   ; initial PC
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash

; ===========================================================================
; SEGA HEADER (at $000100)
; ===========================================================================
        ORG     $100
        dc.b    "SEGA GENESIS    "
        dc.b    "(C)VOID 2026.AUG"
        dc.b    "REGISTER TEST 68K REGISTER TEST 68K R"
        dc.b    "REGISTER TEST 68K REGISTER TEST 68K R"
        dc.b    "GM 00000000-00"
        dc.w    0
        dc.b    "J               "
        dc.l    0,0
        dc.l    $FF0000,$FFFFFF
        dc.b    "            "
        dc.b    "J               "
        dc.b    "            "
        dc.b    "J               "
        dc.b    $00,$00,$00,$00,$00,$00,$00,$00
        dc.b    $00,$00,$00,$00,$00,$00,$00,$00
        dc.b    $00,$00,$00,$00,$00,$00,$00,$00

; ===========================================================================
; CODE (at $000200)
; ===========================================================================
        ORG     $200

Crash:  bra.s   Crash

Entry:
        ; ---- VDP register init (S1-style table) ----
        lea     VDPRegs(pc),a0
        moveq   #24-1,d7
        move.w  #$8000,d0
.regloop:
        move.b  (a0)+,d0
        move.w  d0,VDP_CTRL
        add.w   #$100,d0
        dbf     d7,.regloop

        ; ---- clear plane A (40x28 cells at word $C000) ----
        move.l  #$40000003,VDP_CTRL
        move.w  #40*28-1,d0
.clr:
        move.w  #0,VDP_DATA
        dbf     d0,.clr

        ; ---- load font tiles (96 x 16 words at word addr $2000) ----
        lea     FontTiles(pc),a0
        move.l  #$40002000,d0
        moveq   #96-1,d7
.tile:
        move.l  d0,VDP_CTRL
        moveq   #16-1,d6
.words:
        moveq   #0,d1
        move.b  (a0)+,d1
        lsl.w   #8,d1
        move.b  (a0)+,d1
        move.w  d1,VDP_DATA
        dbf     d6,.words
        add.l   #$00100000,d0
        dbf     d7,.tile

        ; ---- palette: color0 black, color1 white ----
        move.l  #$C0000000,VDP_CTRL
        move.w  #0,VDP_DATA
        move.w  #$0EEE,VDP_DATA
        moveq   #14-1,d0
.pal:
        move.w  #0,VDP_DATA
        dbf     d0,.pal

        ; ---- display on ----
        move.w  #$8115,VDP_CTRL

        ; ---- read console version ----
        move.b  VERSION_REG,d0
        andi.b  #$0F,d0
        move.b  d0,VDP_BUF

        ; ---- PSG blip ----
        move.b  #$80,PSG
        move.b  #$01,PSG
        move.b  #$90,PSG
        lea     $007FFFFF,a0
.delay:
        dbf     d0,.delay
        move.b  #$9F,PSG

        ; ---- static text ----
        lea     StrTitle(pc),a0
        moveq   #0,d0
        bsr     PrintStr
        lea     StrLabels(pc),a0
        move.w  #4*40,d0
        bsr     PrintStr
        lea     StrALabels(pc),a0
        move.w  #9*40,d0
        bsr     PrintStr
        lea     StrHelp1(pc),a0
        move.w  #14*40,d0
        bsr     PrintStr
        lea     StrHelp2(pc),a0
        move.w  #15*40,d0
        bsr     PrintStr

        ; ---- run test 0 once ----
        move.b  #0,TEST_BUF
        bsr     RunTest

; ===========================================================================
; MAIN LOOP: read pad, switch test, run, redraw
; ===========================================================================
MainLoop:
        bsr     ReadPad                 ; d0 = U D L R A B C S (active high)
        move.b  PAD_BUF,d1
        move.b  d0,PAD_BUF
        move.b  d0,d2
        not.b   d1
        and.b   d1,d2                   ; d2 = newly pressed bits

        btst    #1,d2                   ; DOWN -> next test
        beq.s   .chkup
        addq.b  #1,TEST_BUF
        cmpi.b  #4,TEST_BUF
        blo.s   .run
        move.b  #0,TEST_BUF
        bra.s   .run
.chkup:
        btst    #0,d2                   ; UP -> previous test
        beq.s   .chkrun
        subq.b  #1,TEST_BUF
        bcc.s   .run
        move.b  #3,TEST_BUF
        bra.s   .run
.chkrun:
        btst    #4,d2                   ; A
        bne.s   .run
        btst    #7,d2                   ; START
        beq.s   .draw
.run:
        bsr     RunTest
.draw:
        bsr     DrawPad
        bra.s   MainLoop

; ===========================================================================
; RunTest: dispatch to test 0-3, store registers, refresh display
; ===========================================================================
RunTest:
        moveq   #0,d0
        move.b  TEST_BUF,d0
        add.w   d0,d0
        move.w  TestTable(pc,d0.w),d1
        jsr     TestTable(pc,d1.w)
        move.b  TEST_BUF,d0
        add.w   d0,d0
        move.w  TestNames(pc,d0.w),d1
        lea     TestNames(pc,d1.w),a0
        move.w  #14*40+10,d0
        bsr     PrintStr
        ; print test number
        move.w  #14*40+7,d0
        moveq   #0,d1
        move.b  TEST_BUF,d1
        addi.b  #$30,d1
        bsr     PrintChar
        ; redraw all register rows
        bsr     DrawRegs
        rts

TestTable:
        dc.w    Test1-TestTable
        dc.w    Test2-TestTable
        dc.w    Test3-TestTable
        dc.w    Test4-TestTable
TestNames:
        dc.w    TName1-TestNames
        dc.w    TName2-TestNames
        dc.w    TName3-TestNames
        dc.w    TName4-TestNames
TName1: dc.b    "ADD / SUB",0
TName2: dc.b    "SHIFT / ROTATE",0
TName3: dc.b    "MUL / DIV",0
TName4: dc.b    "MEMORY / MOVEM",0
        align   2

; ===========================================================================
; TEST 1: ADD/SUB
; ===========================================================================
Test1:
        move.l  #$12345678,d0
        move.l  #$11111111,d1
        add.l   d1,d0                   ; d0=$23456789
        move.l  d0,d2
        sub.l   d1,d2                   ; d2=$12345678
        move.l  d0,d3
        add.l   d0,d3                   ; d3=$468ACF12
        moveq   #0,d4
        sub.l   d1,d4                   ; d4=$EEEEEEEF
        move.l  d4,d5
        addq.l  #1,d5                   ; d5=$EEEEEEF0
        moveq   #-1,d6
        clr.l   d7
        move.l  d0,a0
        move.l  d1,a1
        move.l  d2,a2
        move.l  d3,a3
        lea     $00FF0000,a4
        lea     $00FF1000,a5
        lea     $00FFFFF0,a6
        bra.w   StoreRegs

; ===========================================================================
; TEST 2: SHIFT/ROTATE
; ===========================================================================
Test2:
        move.l  #$12345678,d0
        move.l  d0,d1
        lsl.l   #4,d1                   ; $23456780
        move.l  d0,d2
        lsr.l   #4,d2                   ; $01234567
        move.l  d0,d3
        rol.l   #8,d3                   ; $34567812
        move.l  d0,d4
        ror.l   #8,d4                   ; $78123456
        move.l  d0,d5
        lsl.l   #1,d5                   ; $2468ACF0
        move.l  #$80000000,d6
        asr.l   #4,d6                   ; $F8000000
        move.l  d0,d7
        swap    d7                      ; $56781234
        move.l  d0,a0
        move.l  d1,a1
        move.l  d2,a2
        move.l  d3,a3
        lea     $00FF0000,a4
        move.l  a4,a5
        move.l  a5,a6
        bra.w   StoreRegs

; ===========================================================================
; TEST 3: MUL/DIV
; ===========================================================================
Test3:
        move.l  #$1234,d0
        mulu.w  #$1234,d0               ; $14B45A90
        move.l  #$12345678,d1
        divu.w  #$100,d1                ; q=$1234 r=$78 -> $12340078
        move.l  #$FFFFFFFF,d2
        lsr.l   #8,d2                   ; $00FFFFFF
        move.l  d2,d3
        add.l   d0,d3                   ; $14B45A8F? (0xFF0000+...)
        moveq   #7,d4
        mulu.w  d4,d4                   ; $31
        move.l  #100,d5
        divu.w  #7,d5                   ; q=$E r=2 -> $000E0002
        moveq   #0,d6
        moveq   #0,d7
        move.l  d0,a0
        move.l  d1,a1
        move.l  d5,a2
        move.l  d4,a3
        lea     $00FF0000,a4
        lea     $00FF0004,a5
        lea     $00FF0008,a6
        bra.w   StoreRegs

; ===========================================================================
; TEST 4: MEMORY / MOVEM
; ===========================================================================
Test4:
        lea     REG_BUF,a4
        move.l  #$DEADBEEF,d0
        move.l  d0,(a4)
        move.l  #$CAFEBABE,d1
        move.l  d1,4(a4)
        move.l  #$12345678,d2
        move.l  d2,8(a4)
        move.l  (a4),d3
        move.l  4(a4),d4
        move.l  8(a4),d5
        move.l  #$AAAAAAAA,d6
        move.l  #$55555555,d7
        move.l  d0,a0
        move.l  d1,a1
        move.l  d2,a2
        move.l  a4,a3
        lea     4(a4),a5
        lea     8(a4),a6
        bra.w   StoreRegs

; ===========================================================================
StoreRegs:
        movem.l d0-d7,REG_BUF
        movem.l a0-a6,REG_BUF+32
        move.l  sp,REG_BUF+60
        rts

; ===========================================================================
; ReadPad: d0 = U(0) D(1) L(2) R(3) A(4) B(5) C(6) S(7), active high
; ===========================================================================
ReadPad:
        move.b  #$40,PAD1
        nop
        nop
        move.b  PAD1,d0
        not.b   d0
        andi.b  #$0F,d0
        move.b  #$00,PAD1
        nop
        nop
        move.b  PAD1,d1
        not.b   d1
        andi.b  #$0F,d1
        lsl.b   #4,d1
        or.b    d1,d0
        rts

; ===========================================================================
; PrintChar: d0 = cell index, d1 = char code (ASCII)
; ===========================================================================
PrintChar:
        move.b  d1,d1
        beq.s   .done
        subi.b  #$20,d1
        ext.w   d1
        move.l  d0,d2
        swap    d2
        or.l    #$40000003,d2
        move.l  d2,VDP_CTRL
        move.w  d1,VDP_DATA
        addq.w  #1,d0
.done:  rts

; ===========================================================================
; PrintStr: d0 = cell index, a0 = null-terminated string
; ===========================================================================
PrintStr:
        move.w  d0,-(sp)
.loop:
        move.b  (a0)+,d1
        beq.s   .done
        move.w  (sp),d0
        bsr     PrintChar
        addq.w  #1,(sp)
        bra.s   .loop
.done:
        addq.w  #2,sp
        rts

; ===========================================================================
; DrawPad: render pad state + VDP version on row 2
; ===========================================================================
DrawPad:
        lea     PAD_STR,a0
        moveq   #0,d0
        move.b  PAD_BUF,d0
        lea     CharsUDLR,a1
        moveq   #0,d7
.d1:
        move.b  (a1)+,d2
        move.l  d7,d3
        btst    d3,d0
        beq.s   .off1
        move.b  d2,(a0)+
        bra.s   .next1
.off1:
        move.b  #$2D,(a0)+              ; '-'
.next1:
        addq.w  #1,d7
        cmpi.w  #4,d7
        blo.s   .d1
        move.b  #$20,(a0)+
        lea     CharsSABC,a1
        moveq   #0,d7
        moveq   #4,d6
.d2:
        move.b  (a1)+,d2
        move.l  d6,d3
        btst    d3,d0
        beq.s   .off2
        move.b  d2,(a0)+
        bra.s   .next2
.off2:
        move.b  #$2D,(a0)+
.next2:
        addq.w  #1,d6
        addq.w  #1,d7
        cmpi.w  #4,d7
        blo.s   .d2
        move.b  #0,(a0)

        lea     StrPad0(pc),a0
        move.w  #2*40,d0
        bsr     PrintStr
        lea     PAD_STR,a0
        move.w  #2*40+16,d0
        bsr     PrintStr

        ; VDP version
        move.w  #2*40+6,d0
        moveq   #0,d1
        move.b  VDP_BUF,d1
        bsr     PrintHex
        rts

CharsUDLR:      dc.b    "UDLR"
CharsSABC:      dc.b    "ABCS"

; ===========================================================================
; PrintHex: d0 = cell index, d1 = 32-bit value
; ===========================================================================
PrintHex:
        move.w  d0,-(sp)
        moveq   #8-1,d7
.hexloop:
        rol.l   #4,d1
        move.l  d1,d2
        andi.w  #$F,d2
        lea     HexDigits(pc),a1
        move.b  (a1,d2.w),d2
        move.w  (sp),d0
        move.l  d1,-(sp)
        move.l  d2,d1
        bsr     PrintChar
        move.l  (sp)+,d1
        addq.w  #1,(sp)
        dbf     d7,.hexloop
        addq.w  #2,sp
        rts

HexDigits:      dc.b    "0123456789ABCDEF"

; ===========================================================================
; DrawRegs: render D0-D7 and A0-A7 from REG_BUF
; ===========================================================================
DrawRegs:
        lea     REG_BUF,a2
        lea     DLabels(pc),a3
        moveq   #0,d6
.drow:
        move.w  d6,d0
        mulu.w  #40,d0
        addq.w  #4,d0
        move.l  a3,a0
        bsr     PrintStr
        add.w   #12,d0
        move.l  (a2)+,d1
        bsr     PrintHex
        lea     12(a3),a3
        move.w  d6,d0
        mulu.w  #40,d0
        addq.w  #4,d0
        add.w   #20,d0
        move.l  a3,a0
        bsr     PrintStr
        add.w   #12,d0
        move.l  (a2)+,d1
        bsr     PrintHex
        lea     12(a3),a3
        addq.w  #1,d6
        cmpi.w  #4,d6
        blo.s   .drow
        ; A regs rows 9-12
        lea     ALabels(pc),a3
        moveq   #0,d6
.arow:
        move.w  d6,d0
        mulu.w  #40,d0
        add.w   #9,d0
        move.l  a3,a0
        bsr     PrintStr
        add.w   #12,d0
        move.l  (a2)+,d1
        bsr     PrintHex
        lea     12(a3),a3
        move.w  d6,d0
        mulu.w  #40,d0
        add.w   #9,d0
        add.w   #20,d0
        move.l  a3,a0
        bsr     PrintStr
        add.w   #12,d0
        move.l  (a2)+,d1
        bsr     PrintHex
        lea     12(a3),a3
        addq.w  #1,d6
        cmpi.w  #4,d6
        blo.s   .arow
        rts

; label tables: 8 entries x 12 bytes each
DLabels:
        dc.b    "D0=        ",0
        dc.b    "D1=        ",0
        dc.b    "D2=        ",0
        dc.b    "D3=        ",0
        dc.b    "D4=        ",0
        dc.b    "D5=        ",0
        dc.b    "D6=        ",0
        dc.b    "D7=        ",0
        align   2
ALabels:
        dc.b    "A0=        ",0
        dc.b    "A1=        ",0
        dc.b    "A2=        ",0
        dc.b    "A3=        ",0
        dc.b    "A4=        ",0
        dc.b    "A5=        ",0
        dc.b    "A6=        ",0
        dc.b    "A7=        ",0
        align   2

; ===========================================================================
; Strings
; ===========================================================================
StrTitle:       dc.b    " SEGA GENESIS - 68K REGISTER TEST",0
StrLabels:      dc.b    "D-REGS",0
StrALabels:     dc.b    "A-REGS",0
StrHelp1:       dc.b    "TEST:0 - ADD / SUB",0
StrHelp2:       dc.b    " U/D=SELECT  A/START=RUN",0
StrPad0:        dc.b    " VDP:   PAD:      ",0
        align   2

; ===========================================================================
; VDP register table (24 bytes)
; ===========================================================================
VDPRegs:
        dc.b    4                       ; $80 mode
        dc.b    $14                     ; $81 display off, DMA on
        dc.b    $30                     ; $82 plane A @ $C000
        dc.b    $3C                     ; $83 window @ $F000
        dc.b    $18                     ; $84 plane B @ $6000
        dc.b    $6C                     ; $85 sprites @ $D800
        dc.b    0,0,0,0
        dc.b    $FF,0
        dc.b    $81                     ; $8C 40-cell
        dc.b    $37,0
        dc.b    1                       ; $8F increment 1
        dc.b    1,0,0
        dc.w    $FFFF
        dc.w    0
        dc.b    $80

        INCLUDE "font.inc"
