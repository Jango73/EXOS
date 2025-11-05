ENTRY(start)

SECTIONS
{
    __kernel_vma = DEFINED(__VMA_KERNEL) ? __VMA_KERNEL : 0xC0000000;

    /* Code */
    . = __kernel_vma;
    .text : {
        *(.text.stub)
        *(.text .text.*)
        *(.rodata .rodata.* .rdata .rdata.*)
    }

    /* TaskRunner stub mapped for user mode */
    . = ALIGN(0x1000);
    .shared_text : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 0x1000)) {
        __task_runner_start = .;
        *(.shared_text)
        __task_runner_end = .;
    }

    /* Data — aligned after task runner */
    . = ALIGN(0x1000);
    .data : AT(ALIGN(LOADADDR(.shared_text) + SIZEOF(.shared_text), 0x1000)) {
        *(.data)
        *(.end_mark)
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
