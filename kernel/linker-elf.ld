ENTRY(start)

SECTIONS
{
    /* Code */
    . = 0xC0000000;
    .text : {
        *(.text.stub)
        *(.text .text.*)
        *(.rodata .rodata.* .rdata .rdata.*)
    }

    /* Data — aligned after .text */
    . = ALIGN(0x1000);
    .data : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 0x1000)) {
        *(.data)
    }

    /* BSS */
    . = ALIGN(0x1000);
    .bss : AT(ALIGN(LOADADDR(.data) + SIZEOF(.data), 0x1000)) {
        __bss_init_start = .;
        *(global)
        *(.bss)
        *(COMMON)
        __bss_init_end = .;
    }
}
