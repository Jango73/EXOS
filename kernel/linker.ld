ENTRY(StartAbsolute)  /* ou kernel_main, selon ton point d’entrée */

SECTIONS
{
    /* Kernel virtuel à partir de 0xC0000000 */
    . = 0xC0000000;

    .text ALIGN(0x1000) : {
        *(.text .text.*)
    }

    .rodata ALIGN(0x1000) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(0x1000) : {
        *(.data .data.*)
    }

    .bss ALIGN(0x1000) : {
        *(.bss COMMON)
        *(global)     /* si tu utilises ce segment explicitement */
    }

    /* Pile optionnelle – en général allouée à l’exécution */
    .stack ALIGN(0x1000) : {
        *(.stack)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note)
    }
}
