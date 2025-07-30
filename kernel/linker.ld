OUTPUT_FORMAT("binary")
ENTRY(StartAbsolute)

SECTIONS
{
    /* Stub — low memory, start of file */
    . = 0x00000000;
    .stub : AT(0x00000000) {
        *(.text.stub)
    }

    /* Kernel — high memory, physically after stub (0x1000 offset) */
    . = 0xC0000000;
    .text : AT(ALIGN(LOADADDR(.stub) + SIZEOF(.stub), 0x1000)) {
        *(.text)
        *(.text.*)
        *(.text.unlikely)
        *(.rodata)
        *(.rodata.*)
        __etext = .;
    }

    /* Data — aligned after .text */
    .data : AT(ALIGN(LOADADDR(.text) + (__etext - ADDR(.text)), 0x10)) {
        *(.data)
    }

    /* BSS */
    .bss : {
        *(.bss)
        *(COMMON)
    }

    /* Stack */
    .stack : {
        *(.stack)
    }
}
