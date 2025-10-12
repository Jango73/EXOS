OUTPUT_FORMAT("binary")
ENTRY(_start)

SECTIONS
{
    . = 0;

    __payload_start = .;
    .start  : { *(.start) }
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : {
        *(.data*)
        *(.bss*)
        *(COMMON)
    }
    __payload_end = .;
}
