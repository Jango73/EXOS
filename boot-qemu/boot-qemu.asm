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
    mov [boot_drive], dl     ; Save BIOS drive number
    sti

    ; Clear screen
    call ClearScreen

    ; Print drive
    mov al, [boot_drive]
    call ByteToASCII

    ; Step A
    mov al, 'A'
    call PrintChar

    ; Load SuperBlock (LBA 2 → 0x0600)
    mov bx, 0x0600
    mov cx, 2              ; LBA
    mov si, 1              ; sector count
    call ReadSectorsLBA

    ; Step B
    mov al, 'B'
    call PrintChar

    ; Read kernel binary starting at LBA 4 → into physical 0x00012000
    ; Load Kernel (LBA 4 → 0x1200:0000)
    mov ax, 0x1200
    mov es, ax
    xor bx, bx
    mov cx, 4              ; LBA
    mov si, NUM_SECTORS
    call ReadSectorsLBA

    ; Step C
    mov al, 'C'
    call PrintChar

    ; Jump to kernel
    jmp far [KernelEntry]

.halt:
    mov al, 'Z'
    call PrintChar
    cli
    hlt
    jmp .halt

;-----------------------------------------------------------
; BIOS INT13 Extensions (AH=42h)
; ReadSectorsLBA:
; IN: es:bx = destination address
;     cx = LBA
;     si = sector count

ReadSectorsLBA:
    pusha

    mov di, dap_packet
    mov byte [di], 0x10         ; size of packet (16 bytes)
    mov byte [di+1], 0          ; reserved
    mov word [di+2], si         ; sector count
    mov word [di+4], bx         ; offset
    mov word [di+6], es         ; segment
    xor eax, eax
    mov ax, cx
    mov dword [di+8], eax       ; LBA low  (32 bits)
    mov dword [di+12], 0        ; LBA high (always 0 here)

    mov si, di
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .fail

    popa
    ret

.fail:
    jmp .halt

.halt:
    mov al, 'X'
    call PrintChar
    cli
    hlt
    jmp .halt

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
; ByteToASCII: convert AL (8 bits) en 2 caractères ASCII hex
; Affiche directement le résultat (haut nibble, puis bas nibble)
ByteToASCII:
    pusha
    mov ah, al
    shr al, 4
    call NibbleToChar
    call PrintChar
    mov al, ah
    and al, 0x0F
    call NibbleToChar
    call PrintChar
    popa
    ret

; Convertit nibble dans AL en caractère ASCII
NibbleToChar:
    add al, '0'
    cmp al, '9'
    jbe .ok
    add al, 7
.ok:
    ret

;-----------------------------------------------------------

times 510-($-$$) db 0
dw 0xAA55

boot_drive:
    db 0

KernelEntry:
    dw 0x0000          ; Offset
    dw 0x1200          ; Segment (0x00012000 >> 4)

dap_packet:
    times 16 db 0      ; Disk Address Packet (16 bytes)
