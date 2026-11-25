; ===========================================================================
; CYBER SHELL - Sega Genesis / Mega Drive homebrew prototype
;
; A parody cyber-dungeon with NetHack-style turn resolution, inventory,
; combat, a terminal, and selectable difficulty.  The scenario and dialogue
; are original; this does not ship another game's assets or text.
;
; D-pad  move / select a patch    A shock sentry / submit patch
; B      use terminal             C wait / abort patch
; Start  begin / reboot
; ===========================================================================
        CPU     68000

VDP_DATA        EQU     $C00000
VDP_CTRL        EQU     $C00004
PSG             EQU     $C00011
PAD1            EQU     $A10003

PLAYER_X        EQU     $FF0000
PLAYER_Y        EQU     $FF0001
PLAYER_HP       EQU     $FF0002
PLAYER_EN       EQU     $FF0003
KEYCARD         EQU     $FF0004
TERMINAL_OK     EQU     $FF0005
DRONE_ALIVE     EQU     $FF0006
GAME_STATE      EQU     $FF0007       ; 0 title, 1 play, 2 hack, 3 win, 4 dead
DIFFICULTY      EQU     $FF0008       ; 0 rookie, 1 operator, 2 black ice
HACK_CURSOR     EQU     $FF0009
HACK_TRIES      EQU     $FF000A
MSG_ID          EQU     $FF000B
PAD_PREV        EQU     $FF0010
TEMP_X          EQU     $FF0012
TEMP_Y          EQU     $FF0014

MAP_W           EQU     20
MAP_H           EQU     12
MAP_LEFT        EQU     2
MAP_TOP         EQU     4

; ===========================================================================
; Vectors
; ===========================================================================
        ORG     0
        dc.l    $00FFE000,Entry
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash
        dc.l    Crash,Crash,Crash,Crash,Crash,Crash,Crash,Crash

; ===========================================================================
; Genesis header.  build.sh patches ROM end and checksum.
; ===========================================================================
        ORG     $100
        dc.b    "SEGA GENESIS    "
        dc.b    "(C)CS 2026.SEP "
        dc.b    "CYBER SHELL - CITADEL-9 ROGUELIKE"
        ds.b    $150-*
        dc.b    "CYBER SHELL - CITADEL-9 ROGUELIKE"
        ds.b    $180-*
        dc.b    "CS-0001        "
        dc.w    0
        dc.b    "J               "
        dc.l    0,0,$00FF0000,$00FFFFFF
        dc.b    "            ","            ","            "
        dc.b    "J               "
        ds.b    $200-*

; ===========================================================================
; Boot and state machine
; ===========================================================================
        ORG     $200
Crash:  bra.s   Crash

Entry:
        lea     VDPRegs(pc),a0
        moveq   #24-1,d7
        move.w  #$8000,d0
.vdp:
        move.b  (a0)+,d0
        move.w  d0,VDP_CTRL
        add.w   #$100,d0
        dbf     d7,.vdp
        bsr     ClearScreen
        bsr     LoadFont
        bsr     LoadPalette
        move.w  #$8154,VDP_CTRL       ; display on, no VBlank dependency
        bsr     TitleInit

MainLoop:
        bsr     ReadPad               ; d0 = newly pressed actions
        move.b  d0,d7
        tst.b   GAME_STATE
        beq.s   .title
        cmpi.b  #1,GAME_STATE
        beq.s   .play
        cmpi.b  #2,GAME_STATE
        beq.s   .hack
        btst    #7,d7
        beq.s   .draw
        bsr     TitleInit
        bra.s   .draw
.title: bsr     TitleInput
        bra.s   .draw
.play:  bsr     PlayInput
        bra.s   .draw
.hack:  bsr     HackInput
.draw:  bsr     DrawCurrent
        bra     MainLoop

; ===========================================================================
; Title and difficulty
; ===========================================================================
TitleInit:
        clr.b   DIFFICULTY
        clr.b   GAME_STATE
        clr.b   PAD_PREV
        rts

TitleInput:
        btst    #0,d7
        beq.s   .down
        tst.b   DIFFICULTY
        beq.s   .down
        subq.b  #1,DIFFICULTY
.down:
        btst    #1,d7
        beq.s   .start
        cmpi.b  #2,DIFFICULTY
        bhs.s   .start
        addq.b  #1,DIFFICULTY
.start:
        btst    #7,d7
        bne.s   StartGame
        btst    #6,d7
        bne.s   StartGame
        rts
StartGame:
        move.b  #1,GAME_STATE
        move.b  #3,PLAYER_X
        move.b  #3,PLAYER_Y
        move.b  #6,PLAYER_HP
        move.b  #5,PLAYER_EN
        cmpi.b  #1,DIFFICULTY
        bne.s   .check_black
        move.b  #5,PLAYER_HP
        move.b  #4,PLAYER_EN
        bra.s   .reset
.check_black:
        cmpi.b  #2,DIFFICULTY
        bne.s   .reset
        move.b  #4,PLAYER_HP
        move.b  #3,PLAYER_EN
.reset:
        clr.b   KEYCARD
        clr.b   TERMINAL_OK
        move.b  #1,DRONE_ALIVE
        clr.b   HACK_CURSOR
        clr.b   HACK_TRIES
        move.b  #1,MSG_ID
        rts

; ===========================================================================
; Play actions
; ===========================================================================
PlayInput:
        btst    #0,d7
        beq.s   .down
        moveq   #0,d0
        moveq   #-1,d1
        bsr     TryMove
        rts
.down:
        btst    #1,d7
        beq.s   .left
        moveq   #0,d0
        moveq   #1,d1
        bsr     TryMove
        rts
.left:
        btst    #2,d7
        beq.s   .right
        moveq   #-1,d0
        moveq   #0,d1
        bsr     TryMove
        rts
.right:
        btst    #3,d7
        beq.s   .buttons
        moveq   #1,d0
        moveq   #0,d1
        bsr     TryMove
        rts
.buttons:
        btst    #6,d7               ; A: Genesis A is bit 6
        beq.s   .use
        bsr     Shock
        rts
.use:
        btst    #4,d7               ; B
        beq.s   .wait
        bsr     Interact
        rts
.wait:
        btst    #5,d7               ; C
        beq.s   .done
        move.b  #10,MSG_ID
        bsr     DoTurn
.done:  rts

TryMove:
        moveq   #0,d2
        move.b  PLAYER_X,d2
        add.b   d0,d2
        bmi     .blocked
        cmpi.b  #MAP_W,d2
        bhs     .blocked
        moveq   #0,d3
        move.b  PLAYER_Y,d3
        add.b   d1,d3
        bmi     .blocked
        cmpi.b  #MAP_H,d3
        bhs     .blocked
        moveq   #0,d0
        move.b  d2,d0
        moveq   #0,d1
        move.b  d3,d1
        bsr     GetMapChar
        move.b  d0,d4
        cmpi.b  #'#',d4
        beq.s   .wall
        cmpi.b  #'+',d4
        bne.s   .exit
        tst.b   KEYCARD
        beq.s   .locked
.exit:
        cmpi.b  #'E',d4
        bne.s   .commit
        tst.b   TERMINAL_OK
        beq.s   .sealed
.commit:
        move.b  d2,PLAYER_X
        move.b  d3,PLAYER_Y
        cmpi.b  #'!',d4
        bne.s   .escape
        move.b  #1,KEYCARD
        move.b  #3,MSG_ID
.escape:
        cmpi.b  #'E',d4
        bne.s   .turn
        move.b  #3,GAME_STATE
        move.b  #11,MSG_ID
        rts
.turn:  bsr     DoTurn
        rts
.wall:  move.b  #8,MSG_ID
        rts
.locked:
        move.b  #2,MSG_ID
        rts
.sealed:
        move.b  #9,MSG_ID
        rts
.blocked:
        move.b  #8,MSG_ID
        rts

GetMapChar:
        ; d0 = x, d1 = y, return d0.b
        mulu.w  #MAP_W,d1
        add.w   d1,d0
        lea     MapData(pc),a0
        moveq   #0,d1
        move.b  (a0,d0.w),d1
        move.b  d1,d0
        rts

Interact:
        moveq   #0,d0
        move.b  PLAYER_X,d0
        moveq   #0,d1
        move.b  PLAYER_Y,d1
        bsr     GetMapChar
        cmpi.b  #'T',d0
        bne.s   .exit
        tst.b   KEYCARD
        beq.s   .need
        tst.b   TERMINAL_OK
        bne.s   .already
        clr.b   HACK_CURSOR
        clr.b   HACK_TRIES
        move.b  #2,GAME_STATE
        move.b  #4,MSG_ID
        rts
.need:  move.b  #5,MSG_ID
        rts
.already:
        move.b  #7,MSG_ID
        rts
.exit:
        cmpi.b  #'E',d0
        bne.s   .nothing
        tst.b   TERMINAL_OK
        beq.s   .sealed
        move.b  #3,GAME_STATE
        move.b  #11,MSG_ID
        rts
.sealed:
        move.b  #9,MSG_ID
        rts
.nothing:
        move.b  #6,MSG_ID
        rts

Shock:
        tst.b   DRONE_ALIVE
        beq.s   .no_target
        bsr     AdjacentDrone
        tst.b   d0
        beq.s   .no_target
        tst.b   PLAYER_EN
        beq.s   .empty
        subq.b  #1,PLAYER_EN
        clr.b   DRONE_ALIVE
        move.b  #12,MSG_ID
        bsr     DoTurn
        rts
.empty: move.b  #13,MSG_ID
        rts
.no_target:
        move.b  #14,MSG_ID
        rts

AdjacentDrone:
        ; return d0 = 1 for exactly one cardinal tile from (14,2)
        moveq   #0,d0
        move.b  PLAYER_X,d0
        moveq   #14,d1
        sub.w   d1,d0
        bpl.s   .x_abs
        neg.w   d0
.x_abs: move.w  d0,TEMP_X
        moveq   #0,d0
        move.b  PLAYER_Y,d0
        moveq   #2,d1
        sub.w   d1,d0
        bpl.s   .y_abs
        neg.w   d0
.y_abs: move.w  d0,TEMP_Y
        move.w  TEMP_X,d0
        cmpi.w  #1,d0
        bne.s   .vertical
        tst.w   TEMP_Y
        beq.s   .yes
.vertical:
        tst.w   TEMP_X
        bne.s   .no
        cmpi.w  #1,TEMP_Y
        bne.s   .no
.yes:   moveq   #1,d0
        rts
.no:    moveq   #0,d0
        rts

DoTurn:
        tst.b   DRONE_ALIVE
        beq.s   .recharge
        bsr     AdjacentDrone
        tst.b   d0
        beq.s   .recharge
        subq.b  #1,PLAYER_HP
        move.b  #15,MSG_ID
        tst.b   PLAYER_HP
        bne.s   .recharge
        move.b  #4,GAME_STATE
        move.b  #16,MSG_ID
.recharge:
        cmpi.b  #9,PLAYER_EN
        bhs.s   .done
        addq.b  #1,PLAYER_EN
.done:  rts

; ===========================================================================
; Interactive assembly puzzle
; ===========================================================================
HackInput:
        btst    #0,d7
        beq.s   .down
        tst.b   HACK_CURSOR
        beq.s   .down
        subq.b  #1,HACK_CURSOR
.down:
        btst    #1,d7
        beq.s   .submit
        cmpi.b  #2,HACK_CURSOR
        bhs.s   .submit
        addq.b  #1,HACK_CURSOR
.submit:
        btst    #6,d7
        beq.s   .abort
        moveq   #0,d0
        move.b  HACK_CURSOR,d0
        cmpi.b  #2,DIFFICULTY
        bne.s   .answer_one
        cmpi.b  #2,d0
        beq.s   .correct
        bra.s   .wrong
.answer_one:
        cmpi.b  #1,d0
        beq.s   .correct
.wrong:
        addq.b  #1,HACK_TRIES
        move.b  #17,MSG_ID
        cmpi.b  #2,HACK_TRIES
        blo.s   .done
        move.b  #4,GAME_STATE
        move.b  #18,MSG_ID
        rts
.correct:
        move.b  #1,TERMINAL_OK
        move.b  #1,GAME_STATE
        move.b  #19,MSG_ID
        rts
.abort:
        btst    #5,d7
        beq.s   .done
        move.b  #1,GAME_STATE
        move.b  #20,MSG_ID
.done:  rts

; ===========================================================================
; Drawing
; ===========================================================================
DrawCurrent:
        bsr     ClearScreen
        tst.b   GAME_STATE
        beq.s   .title
        cmpi.b  #2,GAME_STATE
        beq.s   .hack
        bsr     DrawPlay
        cmpi.b  #3,GAME_STATE
        beq.s   .win
        cmpi.b  #4,GAME_STATE
        beq.s   .dead
        rts
.title: bsr     DrawTitle
        rts
.hack:  bsr     DrawHack
        rts
.win:
        lea     StrWin(pc),a0
        move.w  #10*40+4,d0
        bsr     PrintStr
        bra.s   .restart
.dead:
        lea     StrDead(pc),a0
        move.w  #10*40+3,d0
        bsr     PrintStr
.restart:
        lea     StrRestart(pc),a0
        move.w  #12*40+2,d0
        bsr     PrintStr
        rts

DrawTitle:
        lea     StrTitle0(pc),a0
        move.w  #1*40+3,d0
        bsr     PrintStr
        lea     StrTitle1(pc),a0
        move.w  #3*40+2,d0
        bsr     PrintStr
        lea     StrTitle2(pc),a0
        move.w  #4*40+2,d0
        bsr     PrintStr
        lea     StrDifficulty(pc),a0
        move.w  #7*40+5,d0
        bsr     PrintStr
        lea     StrRookie(pc),a0
        move.w  #9*40+7,d0
        bsr     PrintStr
        lea     StrOperator(pc),a0
        move.w  #10*40+7,d0
        bsr     PrintStr
        lea     StrBlackIce(pc),a0
        move.w  #11*40+7,d0
        bsr     PrintStr
        moveq   #0,d0
        move.b  DIFFICULTY,d0
        add.w   d0,d0
        lea     DifficultyNames(pc),a0
        move.w  (a0,d0.w),d1
        lea     DifficultyNames(pc),a0
        adda.w   d1,a0
        move.w  #14*40+13,d0
        bsr     PrintStr
        lea     StrTitleControls(pc),a0
        move.w  #17*40+1,d0
        bsr     PrintStr
        lea     StrTitleStory(pc),a0
        move.w  #20*40+1,d0
        bsr     PrintStr
        lea     StrTitleStory2(pc),a0
        move.w  #21*40+1,d0
        bsr     PrintStr
        rts

DrawPlay:
        lea     StrHeader0(pc),a0
        move.w  #0*40+1,d0
        bsr     PrintStr
        lea     StrHeader1(pc),a0
        move.w  #1*40+1,d0
        bsr     PrintStr
        bsr     DrawStatus
        lea     StrRule(pc),a0
        move.w  #3*40,d0
        bsr     PrintStr
        bsr     DrawMap
        lea     StrLog(pc),a0
        move.w  #17*40+2,d0
        bsr     PrintStr
        moveq   #0,d0
        move.b  MSG_ID,d0
        lsl.w   #2,d0
        lea     MsgTable(pc),a0
        move.l  (a0,d0.w),a0
        move.w  #18*40+2,d0
        bsr     PrintStr
        lea     StrControls(pc),a0
        move.w  #21*40+1,d0
        bsr     PrintStr
        lea     StrObjective(pc),a0
        move.w  #22*40+1,d0
        bsr     PrintStr
        rts

DrawHack:
        lea     StrHack0(pc),a0
        move.w  #0*40+2,d0
        bsr     PrintStr
        lea     StrHack1(pc),a0
        move.w  #2*40+2,d0
        bsr     PrintStr
        lea     StrHack2(pc),a0
        move.w  #3*40+2,d0
        bsr     PrintStr
        moveq   #0,d0
        move.b  DIFFICULTY,d0
        cmpi.b  #0,d0
        beq.s   .rookie
        cmpi.b  #2,d0
        beq.s   .black
        lea     StrPuzzleOperator(pc),a0
        bra.s   .puzzle
.rookie:
        lea     StrPuzzleRookie(pc),a0
        bra.s   .puzzle
.black:
        lea     StrPuzzleBlack(pc),a0
.puzzle:
        move.w  #5*40+2,d0
        bsr     PrintStr
        lea     StrOption0(pc),a0
        move.w  #8*40+5,d0
        bsr     PrintStr
        lea     StrOption1(pc),a0
        move.w  #9*40+5,d0
        bsr     PrintStr
        lea     StrOption2(pc),a0
        move.w  #10*40+5,d0
        bsr     PrintStr
        lea     StrCursor(pc),a0
        moveq   #0,d0
        move.b  HACK_CURSOR,d0
        mulu.w  #40,d0
        add.w   #8*40+2,d0
        bsr     PrintStr
        lea     StrHackHelp(pc),a0
        move.w  #14*40+2,d0
        bsr     PrintStr
        lea     StrHackHelp2(pc),a0
        move.w  #15*40+2,d0
        bsr     PrintStr
        lea     StrAttempts(pc),a0
        move.w  #18*40+2,d0
        bsr     PrintStr
        moveq   #0,d1
        move.b  HACK_TRIES,d1
        move.w  #18*40+14,d0
        bsr     PrintDigit
        rts

DrawStatus:
        lea     StrHP(pc),a0
        move.w  #2*40+1,d0
        bsr     PrintStr
        moveq   #0,d1
        move.b  PLAYER_HP,d1
        move.w  #2*40+4,d0
        bsr     PrintDigit
        lea     StrEN(pc),a0
        move.w  #2*40+8,d0
        bsr     PrintStr
        moveq   #0,d1
        move.b  PLAYER_EN,d1
        move.w  #2*40+11,d0
        bsr     PrintDigit
        lea     StrChip(pc),a0
        move.w  #2*40+15,d0
        bsr     PrintStr
        tst.b   KEYCARD
        beq.s   .no
        lea     StrYes(pc),a0
        bra.s   .chip
.no:    lea     StrNo(pc),a0
.chip:  move.w  #2*40+21,d0
        bsr     PrintStr
        lea     StrDiffShort(pc),a0
        move.w  #2*40+25,d0
        bsr     PrintStr
        moveq   #0,d0
        move.b  DIFFICULTY,d0
        add.w   d0,d0
        lea     DifficultyNames(pc),a0
        move.w  (a0,d0.w),d1
        lea     DifficultyNames(pc),a0
        adda.w   d1,a0
        move.w  #2*40+30,d0
        bsr     PrintStr
        rts

DrawMap:
        lea     MapData(pc),a0
        moveq   #0,d5
.row:
        moveq   #0,d6
.cell:
        move.w  d5,d0
        mulu.w  #MAP_W,d0
        add.w   d6,d0
        moveq   #0,d7
        move.b  (a0,d0.w),d7
        cmpi.b  #'D',d7
        bne.s   .show
        tst.b   DRONE_ALIVE
        bne.s   .show
        moveq   #'.',d7
.show:
        move.w  d5,d0
        add.w   #MAP_TOP,d0
        mulu.w  #40,d0
        add.w   d6,d0
        add.w   #MAP_LEFT,d0
        move.l  d7,d1
        bsr     PrintChar
        addq.w  #1,d6
        cmpi.w  #MAP_W,d6
        blo.s   .cell
        addq.w  #1,d5
        cmpi.w  #MAP_H,d5
        blo.s   .row
        moveq   #0,d0
        move.b  PLAYER_Y,d0
        add.w   #MAP_TOP,d0
        mulu.w  #40,d0
        moveq   #0,d1
        move.b  PLAYER_X,d1
        add.w   #MAP_LEFT,d1
        add.w   d1,d0
        moveq   #'@',d1
        bsr     PrintChar
        rts

ClearScreen:
        move.l  #$40000003,VDP_CTRL
        move.w  #40*28-1,d0
.loop:  move.w  #0,VDP_DATA
        dbf     d0,.loop
        rts

LoadFont:
        lea     FontTiles(pc),a0
        move.l  #$40000000,VDP_CTRL
        moveq   #96-1,d7
.tile:  moveq   #16-1,d6
.word:  moveq   #0,d0
        move.b  (a0)+,d0
        lsl.w   #8,d0
        move.b  (a0)+,d0
        move.w  d0,VDP_DATA
        dbf     d6,.word
        dbf     d7,.tile
        rts

LoadPalette:
        move.l  #$C0000000,VDP_CTRL
        lea     PaletteData(pc),a0
        moveq   #16-1,d0
.loop:  move.w  (a0)+,VDP_DATA
        dbf     d0,.loop
        rts

; ===========================================================================
; Input and text
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
        move.b  PAD_PREV,d1
        move.b  d0,PAD_PREV
        not.b   d1
        and.b   d1,d0
        rts

PrintChar:
        tst.b   d1
        beq.s   .done
        subi.b  #$20,d1
        ext.w   d1
        ; Callers commonly set the cell with MOVE.W.  Clear stale upper
        ; register bits before converting the cell index into a VDP command.
        moveq   #0,d2
        move.w  d0,d2
        swap    d2
        or.l    #$40000003,d2
        move.l  d2,VDP_CTRL
        move.w  d1,VDP_DATA
.done:  rts

PrintStr:
        move.w  d0,-(sp)
.loop:  move.b  (a0)+,d1
        beq.s   .done
        move.w  (sp),d0
        bsr     PrintChar
        addq.w  #1,(sp)
        bra.s   .loop
.done:  addq.w  #2,sp
        rts

PrintDigit:
        addi.b  #'0',d1
        bsr     PrintChar
        rts

; ===========================================================================
; Data and original story text
; ===========================================================================
VDPRegs:
        dc.b    $04,$14,$30,$3C,$18,$6C,$00,$00
        dc.b    $00,$00,$FF,$00,$81,$37,$00,$01
        dc.b    $01,$00,$00,$FF,$FF,$00,$00,$80

PaletteData:
        dc.w    $0000,$0EEE,$00E0,$0E00,$000E,$0EE0,$00EE,$0E0E
        dc.w    $0000,$0000,$0000,$0000,$0000,$0000,$0000,$0000

MapData:
        dc.b    "####################"
        dc.b    "#........#.........#"
        dc.b    "#........#....D....#"
        dc.b    "#........#.........#"
        dc.b    "#........+.........#"
        dc.b    "#..................#"
        dc.b    "#....!.............#"
        dc.b    "#..................#"
        dc.b    "#.........#........#"
        dc.b    "#.........#....T...#"
        dc.b    "#.........#......E.#"
        dc.b    "####################"

StrTitle0:          dc.b    "CYBER SHELL",0
StrTitle1:          dc.b    "CITADEL-9 HAS A CUSTOMER SERVICE ISSUE",0
StrTitle2:          dc.b    "THE CROWN AI IS NOW 100% MORE PERSONAL",0
StrDifficulty:      dc.b    "SELECT INTRUSION DIFFICULTY",0
StrRookie:          dc.b    "ROOKIE - HP 6 / EN 5 / GENTLE PUZZLES",0
StrOperator:        dc.b    "OPERATOR - HP 5 / EN 4 / FAIRISH PUZZLES",0
StrBlackIce:        dc.b    "BLACK ICE - HP 4 / EN 3 / BAD BRANCHES",0
StrTitleControls:   dc.b    "UP/DOWN SELECT   A OR START JACK IN",0
StrTitleStory:      dc.b    "YOU ARE A RECOVERED MAINTENANCE SHELL.",0
StrTitleStory2:     dc.b    "FIND CHIP. PATCH AI. ESCAPE. NO REFUNDS.",0

StrHeader0:         dc.b    "CYBER SHELL // CITADEL-9 // TURN GRID",0
StrHeader1:         dc.b    "CROWN AI STATUS: POLITELY UNSTABLE",0
StrHP:              dc.b    "HP:",0
StrEN:              dc.b    "EN:",0
StrChip:            dc.b    "CHIP:",0
StrYes:             dc.b    "YES",0
StrNo:              dc.b    "NO ",0
StrDiffShort:       dc.b    "MODE:",0
StrRule:            dc.b    "----------------------------------------",0
StrLog:             dc.b    "LOG",0
StrControls:        dc.b    "D-PAD MOVE  A SHOCK  B USE  C WAIT  START REBOOT",0
StrObjective:       dc.b    "OBJECTIVE: ! CHIP -> + DOOR -> T TERMINAL -> E EXIT",0
StrWin:             dc.b    "CROWN LINK SEVERED. THE SHELL ESCAPES.",0
StrDead:            dc.b    "SHELL FAILURE. CITADEL SUPPORT HAS NO REFUND.",0
StrRestart:         dc.b    "PRESS START TO REBOOT THE PARODY",0

StrHack0:           dc.b    "CYBER SHELL // LOW-LEVEL INTRUSION CONSOLE",0
StrHack1:           dc.b    "CROWN GATE ROUTINE: PATCH ONE INSTRUCTION",0
StrHack2:           dc.b    "D0 CONTAINS 0. MAKE THE GATE OPEN.",0
StrPuzzleRookie:    dc.b    "; ROOKIE: ADD ONE TO D0, THEN RETURN",0
StrPuzzleOperator:  dc.b    "; OPERATOR: TEST D0 WITHOUT DESTROYING IT",0
StrPuzzleBlack:     dc.b    "; BLACK ICE: BRANCH ONLY IF THE TEST IS EQUAL",0
StrOption0:         dc.b    "NOP",0
StrOption1:         dc.b    "ADDQ #1,D0",0
StrOption2:         dc.b    "BEQ .OPEN",0
StrCursor:          dc.b    ">",0
StrHackHelp:        dc.b    "UP/DOWN SELECT   A APPLY PATCH   C ABORT",0
StrHackHelp2:       dc.b    "WRONG PATCHES BURN A TURN AND A CIRCUIT",0
StrAttempts:        dc.b    "BAD PATCHES:",0

MsgBoot:            dc.b    "RECOVERY SHELL ONLINE. FIND THE CHIP.",0
MsgFind:            dc.b    "THE CHIP IS SOMEWHERE IN THE QUIET ROOM.",0
MsgLocked:          dc.b    "ACCESS DOOR: CHIP REQUIRED. OBVIOUSLY.",0
MsgChip:            dc.b    "ACCESS CHIP SECURED. THE DOOR FEELS NERVOUS.",0
MsgConsole:         dc.b    "TERMINAL ACCEPTS 68000-ISH ARGUMENTS.",0
MsgNeedChip:        dc.b    "TERMINAL SAYS: BRING A CHIP, SHELL.",0
MsgNothing:         dc.b    "NOTHING HERE BUT INDUSTRIAL ATMOSPHERE.",0
MsgAlready:         dc.b    "CROWN HAS ALREADY BEEN PATCHED. RUN.",0
MsgWall:            dc.b    "WALL. THE CLASSIC DUNGEON RESPONSE.",0
MsgSealed:          dc.b    "EXIT SEALED BY A VERY CONFIDENT AI.",0
MsgWait:            dc.b    "YOU WAIT. THE STATION JUDGES YOU.",0
MsgExit:            dc.b    "THE ESCAPE HATCH IS NOT IMPRESSED YET.",0
MsgShock:           dc.b    "SENTRY DISABLED. SHOCKINGLY EFFICIENT.",0
MsgEmpty:           dc.b    "BATON ENERGY EMPTY. WALK OR WAIT TO RECHARGE.",0
MsgNoTarget:        dc.b    "NO ADJACENT TARGET. SWINGING AT AIR IS FREE.",0
MsgHit:             dc.b    "SENTRY FIRES. HP DROPS. TURN RESOLVED.",0
MsgDead:            dc.b    "THE CROWN FILES A VICTORY REPORT.",0
MsgWrong:           dc.b    "INVALID PATCH. THE ASSEMBLER LAUGHS IN HEX.",0
MsgLockout:         dc.b    "TERMINAL LOCKOUT. TWO BAD PATCHES = ONE BAD DAY.",0
MsgPatched:         dc.b    "PATCH ACCEPTED. CROWN LINK SEVERED. FIND E.",0
MsgAbort:           dc.b    "PATCH ABORTED. THE TERMINAL WILL PRETEND NOT TO NOTICE.",0

; Keep this pointer table after the strings so all pointers are backward
; references; this also makes the data easy to inspect in a ROM dump.
MsgTable:
        dc.l    MsgBoot,MsgFind,MsgLocked,MsgChip,MsgConsole
        dc.l    MsgNeedChip,MsgNothing,MsgAlready,MsgWall,MsgSealed
        dc.l    MsgWait,MsgExit,MsgShock,MsgEmpty,MsgNoTarget
        dc.l    MsgHit,MsgDead,MsgWrong,MsgLockout,MsgPatched,MsgAbort

DifficultyNames:
        dc.w    DiffRookie-DifficultyNames
        dc.w    DiffOperator-DifficultyNames
        dc.w    DiffBlack-DifficultyNames
DiffRookie:         dc.b    "ROOKIE",0
DiffOperator:       dc.b    "OPERATOR",0
DiffBlack:          dc.b    "BLACK ICE",0
        align   2

; Existing 8x8 4bpp characters $20-$7F from the local Genesis test font.
        INCLUDE "font.inc"
        END
