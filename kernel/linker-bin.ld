OUTPUT_FORMAT("binary")
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
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    /* Data — aligned after .text */
    .data : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 0x1000)) {
        *(.data)
        *(.stack)
    }

    /* BSS */
    .bss : AT(ALIGN(LOADADDR(.data) + SIZEOF(.data), 0x1000)) {
        __bss_start = .;
        *(.bss)
        __bss_end = .;
    }
}
