ENTRY(_start)

SECTIONS
{
    ASSERT(DEFINED(PAYLOAD_OFFSET), "PAYLOAD_OFFSET must be defined")
    . = PAYLOAD_OFFSET;

    .start  : { *(.start) }
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : {
        *(.data*)
        *(.bss*)
        *(COMMON)
    }
}
