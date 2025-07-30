[org 0x7C00]
bits 16

;-----------------------------------------------------------

%include "kernel_sectors.inc"

;-----------------------------------------------------------

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Clear screen & show step A
    call ClearScreen
    mov al, 'A'
    call PrintChar

    ; Load SuperBlock (1 sector from LBA 2 into 0x0600)
    mov bx, 0x0600
    mov cx, 2
    mov si, 1
    call ReadSectors

    ; Step B
    mov al, 'B'
    call PrintChar

    ; Read kernel binary starting at LBA 4 → into 0x12000:0000
    ; es = 0x1200, bx = 0
    mov ax, 0x1200
    mov es, ax
    xor bx, bx
    mov cx, 4                 ; LBA offset where kernel starts
    mov si, NUM_SECTORS      ; sector count from Makefile
    call ReadSectors

    ; Step C
    mov al, 'C'
    call PrintChar

    ; Jump to kernel
    jmp far [KernelEntry]

.halt:
    cli
    hlt
    jmp .halt

;-----------------------------------------------------------
; Utilities

; ReadSectors:
; IN: es:bx = dest addr, cx = LBA, si = count
ReadSectors:
    pusha
.next:
    push cx
    call LBAtoCHS
    pop cx

    mov ah, 0x02
    mov al, 1              ; read 1 sector
    mov dl, [boot_drive]
    int 0x13
    jc .halt               ; halt on error

    add bx, 512
    inc cx
    dec si
    jnz .next
    popa
    ret
.halt:
    cli
.halt_loop:
    hlt
    jmp .halt_loop

;-----------------------------------------------------------

; LBAtoCHS:
; IN: cx = LBA
; OUT: ch = cyl, cl = sect, dh = head
LBAtoCHS:
    ; assume 16 heads, 63 sectors per track
    ; 1 cyl = 16 * 63 = 1008 sectors
    mov dx, cx
    xor ax, ax
    mov bx, 1008
    div bx                 ; ax = cyl, dx = remain
    mov ch, al             ; cyl

    mov ax, dx
    mov bx, 63
    div bx                 ; ax = head, dx = sect-1
    mov dh, al             ; head
    mov cl, dl
    inc cl                 ; sector = dl + 1

    ret

;-----------------------------------------------------------

PrintChar:
    pusha
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    popa
    ret

;-----------------------------------------------------------

ClearScreen:
    mov ax, 0x0600
    mov bh, 0x07
    mov cx, 0
    mov dx, 0x184F
    int 0x10
    ret

;-----------------------------------------------------------

times 510-($-$$) db 0
dw 0xAA55

boot_drive:
    db 0

KernelEntry:
    dw 0x0000          ; Offset
    dw 0x1200          ; Segment
