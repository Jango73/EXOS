ENTRY(start)

SECTIONS
{
    /* Code */
    . = 0xC0000000;
    .text : {
        *(.text.stub)
        *(.text .text.*)
        *(.rodata .rodata.* .rdata .rdata.*)
    }

    /* TaskRunner stub mapped for user mode */
    . = ALIGN(0x1000);
    .task_runner : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 0x1000)) {
        __task_runner_start = .;
        *(.task_runner)
        __task_runner_end = .;
    }

    /* Data — aligned after task runner */
    . = ALIGN(0x1000);
    .data : AT(ALIGN(LOADADDR(.task_runner) + SIZEOF(.task_runner), 0x1000)) {
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
