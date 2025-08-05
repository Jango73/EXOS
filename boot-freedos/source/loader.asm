[bits 16]
[org 0x100]

;----------------------------------------------------------------------------
;  EXOS
;  Copyright (c) 1999-2025 Jango73
;  All rights reserved
;----------------------------------------------------------------------------

DOS_CALL      equ 0x21
DOS_PRINT     equ 0x09
DOS_OPENFILE  equ 0x3D
DOS_CLOSEFILE equ 0x3E
DOS_READFILE  equ 0x3F
DOS_WRITEFILE equ 0x40
DOS_FILESEEK  equ 0x42

DOS_SEEK_SET  equ 0
DOS_SEEK_CUR  equ 1
DOS_SEEK_END  equ 2

STACK_SIZE equ 128
PAGE_SIZE  equ 4096

;----------------------------------------------------------------------------

bits 16

Main :

    ;--------------------------------------
    ; Adjust registers

    cli
    mov     ax, cs
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    sti

    mov     ax, Stack
    add     ax, STACK_SIZE
    mov     sp, ax
    mov     bp, ax

    ;--------------------------------------

    mov     ah, DOS_PRINT
    mov     dx, Text_Loader
    int     DOS_CALL

    ;--------------------------------------
    ; Open file

    mov     ah, DOS_OPENFILE
    mov     al, 0
    mov     dx, FileName
    int     DOS_CALL
    jnc     OpenFileNext
    jmp     ErrorOpen

OpenFileNext :

    mov     [Handle], ax

    mov     ah, DOS_PRINT
    mov     dx, Text_LoadingFile
    int     DOS_CALL

    ;--------------------------------------
    ; Get file size

    mov     ah, DOS_FILESEEK
    mov     al, DOS_SEEK_END
    mov     bx, [Handle]
    xor     cx, cx
    xor     dx, dx
    int     DOS_CALL
    jnc     GetFileSizeNext1
    jmp     ErrorSeek

GetFileSizeNext1 :

    mov     word [FileSize + 0], ax
    mov     word [FileSize + 2], dx

    mov     ah, DOS_FILESEEK
    mov     al, DOS_SEEK_SET
    mov     bx, [Handle]
    xor     cx, cx
    xor     dx, dx
    int     DOS_CALL
    jnc     GetFileSizeNext2
    jmp     ErrorSeek

GetFileSizeNext2 :

    ;--------------------------------------
    ; Read file

    mov     ah, DOS_PRINT
    mov     dx, Text_ReadingFile
    int     DOS_CALL

    xor     eax, eax
    mov     ax, ds
    shl     eax, 4
    add     eax, FreeData
    shr     eax, 4
    mov     [ReadSeg], ax

    mov     esi, [FileSize]
    mov     bx, [Handle]

ReadFileLoop :

    mov     ecx, PAGE_SIZE
    cmp     ecx, esi
    jg      LessThanPage
    jmp     DoRead

LessThanPage :

    mov     ecx, esi

DoRead :

    push    ds
    mov     ax, [ReadSeg]
    mov     ds, ax
    xor     dx, dx
    mov     ah, DOS_READFILE
    int     DOS_CALL
    pop     ds
    jc      ErrorRead

    push    ax
    xor     eax, eax
    pop     ax
    sub     esi, eax
    add     word [ReadSeg], (PAGE_SIZE >> 4)

    mov     ah, DOS_PRINT
    mov     dx, Text_Dot
    int     DOS_CALL

    cmp     esi, 0
    jg      ReadFileLoop

    mov     ah, DOS_PRINT
    mov     dx, Text_NewLine
    int     DOS_CALL

    ;--------------------------------------
    ; Close file

    mov     ah, DOS_CLOSEFILE
    mov     bx, [Handle]
    int     DOS_CALL

    ;--------------------------------------
    ; Jump to loaded code

    mov     ah, DOS_PRINT
    mov     dx, Text_JumpingToStub
    int     DOS_CALL

    xor     eax, eax
    mov     ax, cs
    shl     eax, 4
    add     eax, FreeData
    shr     eax, 4
    mov     [CodeSeg], ax

    mov     si, CodeOfs
    call    far [si]

    ;--------------------------------------
    ; Adjust registers

    cli
    mov     ax, cs
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    sti

    mov     ax, Stack
    add     ax, STACK_SIZE
    mov     sp, ax
    mov     bp, ax

    mov     ah, DOS_PRINT
    mov     dx, Text_ExitDOS
    int     DOS_CALL

    jmp     Exit

ErrorRead :

    mov     ah, DOS_CLOSEFILE
    mov     bx, [Handle]
    int     DOS_CALL

    mov     ah, DOS_PRINT
    mov     dx, Text_CouldNotReadFile
    int     DOS_CALL

    jmp     Exit

ErrorSeek :

    mov     ah, DOS_CLOSEFILE
    mov     bx, [Handle]
    int     DOS_CALL

    mov     ah, DOS_PRINT
    mov     dx, Text_CouldNotSeekFile
    int     DOS_CALL

    jmp     Exit

ErrorOpen :

    mov     ah, DOS_PRINT
    mov     dx, Text_CouldNotOpenFile
    int     DOS_CALL

Exit :

    mov     ah, 0x4C
    int     DOS_CALL

;----------------------------------------------------------------------------

Stack :
    dd 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    dd 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

CodeOfs  : dw 0
CodeSeg  : dw 0
ReadSeg  : dw 0
Handle   : dw 0
FileSize : dd 0
FileName : db 'EXOS.BIN', 0

Text_Loader :
    db '<< EXOS Loader >>', 10, 13, '$'

Text_LoadingFile :
    db 'Loading file', 10, 13, '$'

Text_ReadingFile :
    db 'Reading file', '$'

Text_Dot :
    db '.', '$'

Text_NewLine :
    db 10, 13, '$'

Text_CouldNotOpenFile :
    db 10, 13, 'Could not open file', 10, 13, '$'

Text_CouldNotSeekFile :
    db 10, 13, 'Could not seek file', 10, 13, '$'

Text_CouldNotReadFile :
    db 10, 13, 'Could not read file', 10, 13, '$'

Text_JumpingToStub :
    db 10, 13, 'Jumping to stub...', 10, 13, '$'

Text_ExitDOS :
    db 'Exiting to DOS...', 10, 13, '$'

;----------------------------------------------------------------------------

ALIGN 16
FreeData :

    dd 0

;----------------------------------------------------------------------------
