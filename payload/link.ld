ENTRY(_start)
SECTIONS {
  . = 0x82000000;
  .text : { *(.text.start) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) *(.sdata*) }
  . = ALIGN(16);
  __bss_start = .;
  .bss : { *(.bss*) *(.sbss*) *(COMMON) }
  . = ALIGN(16);
  __bss_end = .;
  /DISCARD/ : { *(.comment) *(.MIPS.*) *(.reginfo) *(.pdr) *(.gnu.*) *(.note*) }
}
