section .text.stub
bits 32

global start
extern KernelMain

stub_base:

    jmp     start

times (4 - ($ - $$)) db 0

Magic : db 'EXOS'

start:

    mov     al, 0x7A
    out     0x2F8, al

    call    KernelMain

    cli
    hlt
    jmp $
