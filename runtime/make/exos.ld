ENTRY(__start__)

PHDRS
{
    text PT_LOAD FLAGS(5); /* R | X */
    data PT_LOAD FLAGS(6); /* R | W */
}

SECTIONS
{
    /* User program image base */
    . = 0x00400000;
    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
    } :text

    /* Keep writable content in a separate LOAD segment */
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data : {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : {
        *(global)
        *(.bss .bss.*)
        *(COMMON)
    } :data
}
