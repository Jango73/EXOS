BITS 64
default rel

section .text

global StubJumpToImage
global LongModeEntry

%define KERNEL_LOAD_ADDRESS 0x200000
%define TRANSITION_STACK_TOP 0x0000A000

extern VbrLongModeCodeSelector
extern VbrLongModeDataSelector

; Parameters (Microsoft x64 ABI)
; RCX: GDTR
; RDX: Paging structure physical address (CR3)
; R8 : Kernel entry low
; R9 : Kernel entry high
; [RSP+0x20]: Multiboot info pointer
; [RSP+0x28]: Multiboot magic

SerialWriteByte:
    push    rax
    push    rdx
    mov     ah, al
.wait:
    mov     dx, 0x03FD
    in      al, dx
    test    al, 0x20
    jz      .wait
    mov     al, ah
    mov     dx, 0x03F8
    out     dx, al
    pop     rdx
    pop     rax
    ret

SerialWriteString:
    push    rax
    push    rdx
.loop:
    mov     al, [rdi]
    test    al, al
    jz      .done
    call    SerialWriteByte
    inc     rdi
    jmp     .loop
.done:
    pop     rdx
    pop     rax
    ret

SerialWriteHex32:
    push    rax
    push    rbx
    push    rcx
    push    rdx
    mov     edx, eax
    mov     ecx, 8
.hex32_loop:
    mov     eax, edx
    shr     eax, 28
    and     eax, 0x0F
    mov     al, [rel HexDigits + rax]
    call    SerialWriteByte
    shl     edx, 4
    dec     ecx
    jnz     .hex32_loop
    pop     rdx
    pop     rcx
    pop     rbx
    pop     rax
    ret

SerialWriteHex64:
    push    rax
    push    rbx
    mov     rbx, rax
    shr     rax, 32
    call    SerialWriteHex32
    mov     eax, ebx
    call    SerialWriteHex32
    pop     rbx
    pop     rax
    ret

SerialWriteLabelHex32:
    push    rax
    push    rdi
    call    SerialWriteString
    pop     rdi
    pop     rax
    call    SerialWriteHex32
    lea     rdi, [rel NewLine]
    call    SerialWriteString
    ret

SerialWriteLabelHex64:
    push    rax
    push    rdi
    call    SerialWriteString
    pop     rdi
    pop     rax
    call    SerialWriteHex64
    lea     rdi, [rel NewLine]
    call    SerialWriteString
    ret

StubJumpToImage:
    mov     r10d, dword [rsp + 0x28]
    mov     r11d, dword [rsp + 0x30]

    mov     r12, rcx
    mov     r13, rdx
    mov     r14d, r8d
    mov     r15d, r9d

    lea     rdi, [rel StubLogStart]
    call    SerialWriteString

    movzx   eax, word [r12]
    lea     rdi, [rel StubLogGdtrLimit]
    call    SerialWriteLabelHex32

    mov     eax, dword [r12 + 2]
    lea     rdi, [rel StubLogGdtrBase]
    call    SerialWriteLabelHex32

    mov     eax, r13d
    lea     rdi, [rel StubLogCr3]
    call    SerialWriteLabelHex32

    cli

    lgdt        [r12]
    mov         ax, [rel VbrLongModeDataSelector]
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         fs, ax
    mov         gs, ax

    mov         eax, r13d
    mov         cr3, rax
    mov         rsp, TRANSITION_STACK_TOP

    mov         rax, r15
    shl         rax, 32
    or          rax, r14
    mov         r14, rax

    mov         r12d, r10d
    mov         r13d, r11d

    mov         ax, [rel VbrLongModeCodeSelector]
    push        rax
    lea         rax, [LongModeEntry]
    push        rax
    retfq

LongModeEntry:
    mov         ax, [rel VbrLongModeDataSelector]
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         fs, ax
    mov         gs, ax

    mov         rsp, KERNEL_LOAD_ADDRESS
    mov         rbp, rsp

    lea         rdi, [rel LongLogEntered]
    call        SerialWriteString

    mov         rax, r14
    lea         rdi, [rel LongLogKernelEntry]
    call        SerialWriteLabelHex64

    mov         eax, r12d
    lea         rdi, [rel LongLogMultibootPointer]
    call        SerialWriteLabelHex32

    mov         eax, r13d
    lea         rdi, [rel LongLogMultibootMagic]
    call        SerialWriteLabelHex32

    mov         rax, cr0
    lea         rdi, [rel LongLogCr0]
    call        SerialWriteLabelHex64

    mov         rax, cr4
    lea         rdi, [rel LongLogCr4]
    call        SerialWriteLabelHex64

    mov         ecx, 0xC0000080
    rdmsr
    shl         rdx, 32
    or          rax, rdx
    lea         rdi, [rel LongLogEfer]
    call        SerialWriteLabelHex64

    mov         eax, r13d
    mov         rbx, r12
    mov         rdx, r14
    jmp         rdx

section .data

global _fltused
global __fltused
_fltused:
__fltused:
    dq 0

section .rodata

StubLogStart: db "[StubJumpToImage] Start", 0x0D, 0x0A, 0
StubLogGdtrLimit: db "[StubJumpToImage] GdtrLimit=0x", 0
StubLogGdtrBase: db "[StubJumpToImage] GdtrBase=0x", 0
StubLogCr3: db "[StubJumpToImage] Cr3=0x", 0
LongLogEntered: db "[LongModeEntry] Entered", 0x0D, 0x0A, 0
LongLogKernelEntry: db "[LongModeEntry] KernelEntry=0x", 0
LongLogMultibootPointer: db "[LongModeEntry] MultibootPointer=0x", 0
LongLogMultibootMagic: db "[LongModeEntry] MultibootMagic=0x", 0
LongLogCr0: db "[LongModeEntry] Cr0=0x", 0
LongLogCr4: db "[LongModeEntry] Cr4=0x", 0
LongLogEfer: db "[LongModeEntry] Efer=0x", 0
NewLine: db 0x0D, 0x0A, 0
HexDigits: db "0123456789ABCDEF", 0
