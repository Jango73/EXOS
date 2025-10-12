ENTRY(_start)

SECTIONS
{
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
