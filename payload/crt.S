# Payload entry points. The low-RAM stubs jump to fixed slots at the start of the
# image: 0x82000000 once per frame (before the game updates) and 0x82000008 in the
# 2D render pass. Each runs C on our own stack, preserving the game's full 128-bit
# callee-saved GPRs (it saves them with sq) and $f20-$f31 (the game's EABI preserves
# all twelve, while our N32 compiler treats the odd ones as scratch), then tail-calls
# the function displaced by the hook, so the game never notices.
.set noreorder
.set noat

.macro ENTER
    lui   $t9, %hi(saved_sp)
    sw    $sp, %lo(saved_sp)($t9)
    lui   $sp, %hi(stack_top)
    addiu $sp, $sp, %lo(stack_top)
    addiu $sp, $sp, -240
    sq    $s0, 0($sp)
    sq    $s1, 16($sp)
    sq    $s2, 32($sp)
    sq    $s3, 48($sp)
    sq    $s4, 64($sp)
    sq    $s5, 80($sp)
    sq    $s6, 96($sp)
    sq    $s7, 112($sp)
    sq    $fp, 128($sp)
    sq    $ra, 144($sp)
    sq    $gp, 160($sp)
    swc1  $f20, 176($sp)
    swc1  $f21, 180($sp)
    swc1  $f22, 184($sp)
    swc1  $f23, 188($sp)
    swc1  $f24, 192($sp)
    swc1  $f25, 196($sp)
    swc1  $f26, 200($sp)
    swc1  $f27, 204($sp)
    swc1  $f28, 208($sp)
    swc1  $f29, 212($sp)
    swc1  $f30, 216($sp)
    swc1  $f31, 220($sp)
.endm

.macro LEAVE target_hi, target_lo
    lq    $s0, 0($sp)
    lq    $s1, 16($sp)
    lq    $s2, 32($sp)
    lq    $s3, 48($sp)
    lq    $s4, 64($sp)
    lq    $s5, 80($sp)
    lq    $s6, 96($sp)
    lq    $s7, 112($sp)
    lq    $fp, 128($sp)
    lq    $ra, 144($sp)
    lq    $gp, 160($sp)
    lwc1  $f20, 176($sp)
    lwc1  $f21, 180($sp)
    lwc1  $f22, 184($sp)
    lwc1  $f23, 188($sp)
    lwc1  $f24, 192($sp)
    lwc1  $f25, 196($sp)
    lwc1  $f26, 200($sp)
    lwc1  $f27, 204($sp)
    lwc1  $f28, 208($sp)
    lwc1  $f29, 212($sp)
    lwc1  $f30, 216($sp)
    lwc1  $f31, 220($sp)
    lui   $t9, %hi(saved_sp)
    lw    $sp, %lo(saved_sp)($t9)
    lui   $t9, \target_hi
    ori   $t9, $t9, \target_lo
    jr    $t9
    nop
.endm

.section .text.start,"ax"
.globl _start
_start:
    j     frame_entry
    nop
    j     draw_entry
    nop
    j     pad_entry
    nop

frame_entry:
    ENTER
    jal   samp_tick_main
    nop
    LEAVE 0x002b, 0x0920        # CSprite2d::InitPerFrame, displaced at 0x246770

draw_entry:
    ENTER
    jal   samp_draw_main
    nop
    LEAVE 0x002a, 0x86b0        # CFont::DrawFonts, displaced at 0x247318

# The pads must be read before we look at them, so this one calls the displaced
# function first and returns through a low-RAM "jr ra" instead of tail-calling.
pad_entry:
    ENTER
    lui   $t9, 0x0024
    ori   $t9, $t9, 0x79d0      # CPad::UpdatePads, displaced at 0x24338C
    jalr  $t9
    nop
    jal   samp_pad_main
    nop
    LEAVE 0x000f, 0xff60

.section .bss
.align 4
saved_sp: .space 16
.align 4
stack_bottom: .space 0x10000
stack_top:
