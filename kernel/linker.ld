OUTPUT_FORMAT("binary")
ENTRY(StartAbsolute)

SECTIONS {
    . = 0x00000000;

    .stub : {
        *(.text.stub)
    }

    . = ALIGN(0x1000);
    .text : {
        *(.text)
    }

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata)
    }

    . = ALIGN(0x1000);
    .data : {
        *(.data)
    }

    . = ALIGN(0x1000);
    .stack : {
        *(.stack)
    }

    . = ALIGN(0x1000);
    .bss : {
        *(.bss)
    }
}
