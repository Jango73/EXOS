OUTPUT_FORMAT("binary")
ENTRY(StartAbsolute)

SECTIONS
{
    /* Stub — low memory, start of file */
    . = 0x00000000;
    .stub : AT(0x00000000) {
        __stub_init_start = .;
        *(.text.stub)
        *(.text.stub2)
        __stub_init_end = .;
    }

    /* Kernel — high memory, physically after stub (0x1000 offset) */
    . = ALIGN(0x1000);
    . = 0xC0000000;
    .text : AT(ALIGN(LOADADDR(.stub) + SIZEOF(.stub), 0x1000)) {
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
