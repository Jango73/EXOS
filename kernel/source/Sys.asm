
; SYS.ASM

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
