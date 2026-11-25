OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(Reset)
SECTIONS {
  . = 0; .vectors : { KEEP(*(.vectors)) }
  . = 0x100; .header : { KEEP(*(.header)) }
  .text : { *(.text*) *(.rodata*) }
  .data : { *(.data*) }
  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
