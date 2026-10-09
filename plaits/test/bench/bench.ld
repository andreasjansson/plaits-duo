/* Memory layout of the benchmarks (see bench.mk): the code in flash, the
   constant data in a separate region (whose reads are counted by
   emulator.py), the variables and the stack in the 32kB of RAM. */

ENTRY(bench_reset)

MEMORY
{
  RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 32K
  FLASH (rx)      : ORIGIN = 0x08008000, LENGTH = 960K
  CONSTANTS (r)   : ORIGIN = 0x08100000, LENGTH = 1024K
}

SECTIONS
{
  .text : {
    . = ALIGN(16);
    KEEP(*(.bench_entry))
    *(.text) *(.text.*)
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(.init_array)) KEEP(*(.ctors))
    __init_array_end = .;
    . = ALIGN(16);
    _etext = .;
    _sidata = _etext;
  } >FLASH
  .rodata : {
    . = ALIGN(16);
    *(.rodata) *(.rodata*)
    . = ALIGN(16);
  } >CONSTANTS
  .data : AT (_sidata) {
    . = ALIGN(16); _sdata = .;
    *(.data) *(.data.*)
    . = ALIGN(16); _edata = .;
  } >RAM
  .bss : {
    . = ALIGN(16); _sbss = .;
    *(.bss) *(.bss.*) *(COMMON)
    . = ALIGN(16); _ebss = .;
  } >RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
