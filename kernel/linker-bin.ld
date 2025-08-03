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
    }

    /* Data — aligned after .text */
    .data : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 0x1000)) {
        *(.data)
    }

    /* BSS */
    .bss : AT(ALIGN(LOADADDR(.data) + SIZEOF(.data), 0x1000)) {
        __bss_start = .;
        *(global)
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }

    /* Heap */
    .heap : AT(ALIGN(LOADADDR(.bss) + SIZEOF(.bss), 0x1000)) {
        *(.heap)
    }

    /* Stack */
    .stack : AT(ALIGN(LOADADDR(.heap) + SIZEOF(.heap), 0x1000)) {
        *(.stack)
    }
}
