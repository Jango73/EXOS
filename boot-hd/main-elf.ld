ENTRY(_start)

SECTIONS
{
    ASSERT(DEFINED(PAYLOAD_ADDRESS), "PAYLOAD_ADDRESS must be defined")
    . = 0;

    .start  : { *(.start) }
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : {
        *(.data*)
        *(.bss*)
        *(COMMON)
    }
}
