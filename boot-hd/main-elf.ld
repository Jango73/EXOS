ENTRY(_start)

SECTIONS
{
    ASSERT(DEFINED(PAYLOAD_ADDRESS), "PAYLOAD_ADDRESS must be defined")
    . = PAYLOAD_ADDRESS;

    .start  : { *(.start) }
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : {
        *(.data*)
        *(.bss*)
        *(COMMON)
    }
}
