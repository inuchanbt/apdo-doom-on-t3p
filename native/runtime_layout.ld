/* Offline startup/layout fixture ONLY. Hardware callbacks remain placeholders.
 * App size is the exact stock3.0.3 container's existing app.bin allocation.
 * No cache SRAM or speculative PSRAM is counted. Nothing may occupy the
 * lower ROM/IRQ area or the stock top-of-RAM 512-byte update reservation.
 */
ENTRY(t3p_runtime_start)
MEMORY
{
    flash (rx) : ORIGIN = 0x0C000100, LENGTH = 0x165D40
    ram (rwx) : ORIGIN = 0x00101A80, LENGTH = 0x35380
}
SECTIONS
{
    .runtime_entry : { KEEP(*(.runtime_entry)) } > flash
    .text : { *(.text*) *(.rodata*) } > flash
    .game : { KEEP(*(.game)) } > flash
    .data ORIGIN(ram) : AT(ALIGN(LOADADDR(.game)+SIZEOF(.game),4))
    {
        . = ALIGN(4);
        __runtime_data_start = .;
        *(.data*)
        . = ALIGN(4);
        __runtime_data_end = .;
    } > ram
    __runtime_data_load = LOADADDR(.data);
    .driver_flash_budget ALIGN(LOADADDR(.data)+SIZEOF(.data),4) : { . += 0x18000; } > flash
    .boot_state (NOLOAD) : { KEEP(*(.boot_state)) KEEP(*(.boot_snapshot)) } > ram
    .bss ALIGN(16) (NOLOAD) :
    {
        __runtime_bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        __runtime_bss_end = .;
    } > ram
    .driver_ram_budget ALIGN(16) (NOLOAD) : { . += 0x8000; } > ram
    __runtime_heap_start = ALIGN(16);
    __runtime_heap_end = ORIGIN(ram)+LENGTH(ram)-0x4000;
    .runtime_stack ORIGIN(ram)+LENGTH(ram)-0x4000 (NOLOAD) :
    {
        __runtime_stack_begin = .;
        . += 0x4000;
        __runtime_stack_end = .;
    } > ram
    __runtime_flash_end = ADDR(.driver_flash_budget)+SIZEOF(.driver_flash_budget);
    ASSERT(ADDR(.runtime_entry) == 0xC000100, "Wrong app entry")
    ASSERT(__runtime_heap_start <= __runtime_heap_end, "Driver/static RAM overlaps stack")
    ASSERT(__runtime_stack_end == 0x136E00, "Update RAM must remain reserved")
    ASSERT((__runtime_data_start & 3) == 0, "Unaligned data")
    ASSERT((__runtime_data_load & 3) == 0, "Unaligned data source")
    ASSERT(((__runtime_data_end - __runtime_data_start) & 3) == 0, "Data length")
    ASSERT(((__runtime_bss_end - __runtime_bss_start) & 3) == 0, "BSS length")
    ASSERT(__runtime_flash_end <= 0xC165E40, "Stock app budget exceeded")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
