section .text.stub
bits 32

global start
extern KernelMain

stub_base:

    jmp     start

times (4 - ($ - $$)) db 0

Magic : db 'EXOS'

start:

    call    KernelMain

    cli
    hlt
    jmp $
