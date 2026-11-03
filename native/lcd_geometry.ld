/* Keep both original function extents, including the shared pure helpers. */
ENTRY(t3p_probe_draw)
probe_stock_prepare = 0x0c006ec6;
probe_stock_wait = 0x0c006efa;
probe_stock_draw = 0x0c02ec4e;
probe_stock_continue = 0x0c02ec96;
SECTIONS
{
    .probe_draw 0x0c00d638 : {
        KEEP(*(.probe_draw)) KEEP(*(.probe_validate))
    }
    .probe_continue 0x0c01106a : {
        KEEP(*(.probe_continue)) KEEP(*(.probe_pattern))
    }
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.llvm*) }
}
ASSERT(SIZEOF(.probe_draw) <= 0x90, "draw/validation exceed stock function")
ASSERT(SIZEOF(.probe_continue) <= 0xd0, "continue/pattern exceed stock function")
ASSERT(SIZEOF(.data) == 0, "probe must not introduce initialized RAM")
ASSERT(SIZEOF(.bss) == 0, "probe must not introduce static RAM")
