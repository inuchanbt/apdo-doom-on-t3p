/* Offline size/link check only. No startup or hardware drivers are supplied. */
ENTRY(t3p_linkcheck_start)
MEMORY
{
    flash (rx) : ORIGIN = 0x0C000100, LENGTH = 0x1F0000
    ram (rwx) : ORIGIN = 0x00101A80, LENGTH = 0x35380
}
SECTIONS
{
    .text : { *(.text*) *(.rodata*) } > flash
    .game : { KEEP(*(.game)) } > flash
    .data : { *(.data*) } > ram AT > flash
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > ram
    . = ALIGN(16);
    __linkcheck_static_ram_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
