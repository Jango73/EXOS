
; Boot_FD.asm

; This code creates a boot sector for floppy drives

;--------------------------------------------------------------------------

SECTOR_SIZE         equ 0x200
MBR_PARTITION_START equ 0x01BE
MBR_PARTITION_SIZE  equ 0x0010
MBR_PARTITION_COUNT equ 0x0004
SEG_START           equ 0x0000
OFS_START           equ 0x7C00
SEG_RELOC           equ 0x0000
OFS_RELOC           equ 0x0600
VAR_START           equ (OFS_START + SECTOR_SIZE)
BUF_START           equ 0x2000
KERNEL_SIZE         equ 115000
KERNEL_ADJUST       equ (((KERNEL_SIZE / SECTOR_SIZE) + 1) * SECTOR_SIZE)
KERNEL_SECTORS      equ (KERNEL_ADJUST / SECTOR_SIZE)
KERNEL_SEGMENT      equ 0x2000

;--------------------------------------------------------------------------

PART_BYTES_PER_SECTOR       equ (OFS_START + 0x000B)    ; 2 bytes
PART_SECTORS_PER_CLUSTER    equ (OFS_START + 0x000D)    ; 1 byte
PART_RESERVED_SECTORS       equ (OFS_START + 0x000E)    ; 2 bytes
PART_NUM_FATS               equ (OFS_START + 0x0010)    ; 1 byte
PART_NUM_ROOT_ENTRIES       equ (OFS_START + 0x0011)    ; 2 bytes
PART_MEDIA_TYPE             equ (OFS_START + 0x0015)    ; 1 byte
PART_SECTORS_PER_FAT        equ (OFS_START + 0x0016)    ; 2 bytes
PART_SECTORS_PER_TRACK      equ (OFS_START + 0x0018)    ; 2 bytes
PART_NUM_HEADS              equ (OFS_START + 0x001A)    ; 2 bytes
PART_HIDDEN_SECTORS_LOW     equ (OFS_START + 0x001C)    ; 2 bytes
PART_HIDDEN_SECTORS_HIGH    equ (OFS_START + 0x001E)    ; 2 bytes
PART_DRIVE                  equ (OFS_START + 0x0024)    ; 1 byte
PART_FAT_ID                 equ (OFS_START + 0x0036)    ; 8 bytes

ROOT_START_LOW              equ (VAR_START + 0x0000)
ROOT_START_HIGH             equ (VAR_START + 0x0002)
DATA_START_LOW              equ (VAR_START + 0x0004)
DATA_START_HIGH             equ (VAR_START + 0x0006)
LBA2CHS_SECTOR              equ (VAR_START + 0x0008)
LBA2CHS_HEAD                equ (VAR_START + 0x0009)
LBA2CHS_CYLINDER            equ (VAR_START + 0x000A)

ROOT_BUFFER                 equ ((BUF_START + (SECTOR_SIZE * 0)) >> 4)
FAT_BUFFER                  equ ((BUF_START + (SECTOR_SIZE * 1)) >> 4)
DATA_BUFFER                 equ ((BUF_START + (SECTOR_SIZE * 2)) >> 4)

;--------------------------------------------------------------------------

segment .boot

bits 16

Boot :

    jmp     Start

Data :

    db 'EXOS'
    dw 0
    dw 1

Empty :

times (0x40 - ($ - Boot)) db 0

Start :

    ;----------------------------------------------------------------------
    ; Setup stack

    cli
    xor     ax, ax
    mov     ss, ax
    mov     sp, OFS_START
    mov     bp, sp
    sti

    ;----------------------------------------------------------------------
    ; Setup segment registers

    xor     ax, ax
    push    ax
    pop     ds
    push    ax
    pop     es

    ;----------------------------------------------------------------------
    ; Relocate code

    mov     si, OFS_START
    mov     di, OFS_RELOC
    mov     cx, SECTOR_SIZE
    cld
    rep     movsb

    ;----------------------------------------------------------------------
    ; Jump to new code

    db      0xEA                       ; jmp far
    dw      OFS_RELOC + (AfterRelocation - Boot)
    dw      SEG_RELOC

AfterRelocation :

    mov     ax, OFS_RELOC + (Text_LoadingOS - Boot)
    call    Print

;    mov     ax, 0
;    mov     bx, 1
;    mov     cx, 3
;    mov     dx, KERNEL_SECTORS
;    mov     di, KERNEL_SEGMENT
;    call    ReadSectors

    ;----------------------------------------------------------------------
    ; Compute LBA of root

    mov     al, [PART_NUM_FATS]
    mul     word [PART_SECTORS_PER_FAT]
    add     ax, [PART_HIDDEN_SECTORS_LOW]
    adc     dx, [PART_HIDDEN_SECTORS_HIGH]
    add     ax, [PART_RESERVED_SECTORS]
    adc     dx, 0
    mov     [ROOT_START_LOW], ax
    mov     [ROOT_START_HIGH], dx
    mov     [DATA_START_LOW], ax
    mov     [DATA_START_HIGH], dx

    ;----------------------------------------------------------------------
    ; Compute first sector of data

    mov     ax, 32
    mul     word [PART_NUM_ROOT_ENTRIES]
    mov     bx, [PART_BYTES_PER_SECTOR]
    add     ax, bx
    dec     ax
    div     bx
    add     [DATA_START_LOW], ax
    adc     word [DATA_START_HIGH], 0

    ;----------------------------------------------------------------------
    ; Read first root sector

    mov     ax, [ROOT_START_LOW]
    mov     dx, [ROOT_START_HIGH]
    call    LBA2CHS

    mov     al, 1
    xor     bx, bx
    push    word ROOT_BUFFER
    pop     es
    call    BIOSSectors

    ;----------------------------------------------------------------------
    ; Find the EXOS.BIN entry

    mov     cx, [PART_NUM_ROOT_ENTRIES]
    xor     di, di

Find_DirEntry :

    push    cx
    mov     cx, 11
    mov     si, OFS_RELOC + (Text_EXOS_Name - Boot)
    repz    cmpsb
    pop     cx
    jz      Found_DirEntry
    add     di, 32
    loop    Find_DirEntry
    jmp     HangSystem

Found_DirEntry :

    ;----------------------------------------------------------------------
    ; Compute first sector of EXOS.BIN

    push    ds
    push    word ROOT_BUFFER
    pop     ds
    mov     ax, [di + 0x1A]
    pop     ds
    dec     ax
    dec     ax
    mov     bl, [PART_SECTORS_PER_CLUSTER]
    xor     bh, bh
    mul     bx
    add     ax, [ROOT_START_LOW]
    adc     dx, [ROOT_START_HIGH]

    ;----------------------------------------------------------------------
    ; Execute OS

    mov     ax, OFS_RELOC + (Text_ExecutingOS - Boot)
    call    Print

    jmp     HangSystem

    db      0xEA                       ; jmp far
    dw      0x0000
    dw      KERNEL_SEGMENT

;--------------------------------------------------------------------------

LBA2CHS :

; ax = LBA low
; dx = LBA high

    cmp     dx, [PART_SECTORS_PER_TRACK]
    jnb     LBA2CHS_Error
    div     word [PART_SECTORS_PER_TRACK]
    inc     dl
    mov     [LBA2CHS_SECTOR], dl
    xor     dx, dx
    div     word [PART_NUM_HEADS]
    mov     [LBA2CHS_HEAD], dl
    mov     [LBA2CHS_CYLINDER], ax
    clc
    ret

LBA2CHS_Error :

    stc
    ret

;--------------------------------------------------------------------------

ReadSectors :

; ax = Sector low
; dx = Sector High
; cx = Num sectors

    ret

;--------------------------------------------------------------------------

BIOSSectors :

; al = Number of sectors
; es:bx = buffer

    mov     ah, 2
    mov     dx, [LBA2CHS_CYLINDER]
    mov     cl, 6
    shl     dh, cl
    or      dh, [LBA2CHS_SECTOR]
    mov     cx, dx
    xchg    ch, cl
    mov     dl, [PART_DRIVE]
    mov     dh, [LBA2CHS_HEAD]
    int     13
    ret

;--------------------------------------------------------------------------

Print :

    push    si
    mov     si, ax
    cld

Print_Next :

    lodsb
    cmp     al, 0
    je      Print_Out
    push    si
    mov     ah, 0x0E
    mov     bl, 7
    int     0x10
    pop     si
    jmp     Print_Next

Print_Out :

    pop     si
    ret

;--------------------------------------------------------------------------

HangSystem :

    mov     ax, OFS_RELOC + (Text_DiskFailure - Boot)
    call    Print

;--------------------------------------------------------------------------

Hang :

    jmp     Hang

;--------------------------------------------------------------------------

Text_LoadingOS :

    db      'Loading OS...', 10, 13, 0

Text_ExecutingOS :

    db      'Executing OS...', 10, 13, 0

Text_DiskFailure :

    db      'Disk error...', 10, 13, 0

Text_EXOS_Name :

    db  'EXOS    BIN', 0

;--------------------------------------------------------------------------

times (MBR_PARTITION_START - ($ - Boot)) db 0

;--------------------------------------------------------------------------

Partition :

    dd 0, 0, 0, 0
    dd 0, 0, 0, 0
    dd 0, 0, 0, 0
    dd 0, 0, 0, 0

BIOS :

    dw 0xAA55

;--------------------------------------------------------------------------

