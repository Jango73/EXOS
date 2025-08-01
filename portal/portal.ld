OUTPUT_FORMAT("binary")
ENTRY(__start__)

SECTIONS
{
    /* Kernel — high memory, physically after stub (0x1000 offset) */
    . = 0x00400000;
    .text : {
        *(.text)
        *(.rodata)
    }

    /* Data  */
    . = ALIGN(0x10);
    .data : {
        *(.data)
    }

    /* BSS */
    . = ALIGN(0x10);
    .bss : {
        *(global)
        *(.bss)
        *(COMMON)
    }
}
