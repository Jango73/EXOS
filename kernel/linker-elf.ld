ENTRY(StartAbsolute)

SECTIONS
{
    /* Stub — low memory, start of file */
    . = 0x00000000;
    .stub : AT(0x00000000) {
        *(.text.stub)
        *(.text.stub2)
    }

    /* Kernel — high memory, physically after stub (0x1000 offset) */
    . = 0xC0000000;
    .text : AT(ALIGN(LOADADDR(.stub) + SIZEOF(.stub), 0x1000)) {
        *(.text)
        *(.rodata)
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
