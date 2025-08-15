
; sector0.asm

;--------------------------------------------------------------------------

; BIOS reads boot sector at physical address 0x00007C00

STARTSEG equ 0x0000
STARTOFS equ 0x7C00

;--------------------------------------------------------------------------

; We read the SuperBlock at physical address 0x00090000

SUPERSEG equ 0x9000
SUPEROFS equ 0x0000

;--------------------------------------------------------------------------

SECTORSIZE equ 0x0200                  ; (512)

;--------------------------------------------------------------------------

BootSector segment para public use16 "CODE"

  public _Sector0

;--------------------------------------------------------------------------

_Sector0 :

  jmp Start

;--------------------------------------------------------------------------

Data :

  BootDrive db 0
  EXOSMagic db 'EXOS'

;--------------------------------------------------------------------------

DetectCPU :

  ret

;--------------------------------------------------------------------------

Reboot :

; jmp far FFFF:0000

  db 0xEA
  db 0x0000
  db 0xFFFF

;--------------------------------------------------------------------------

Start :

; Setup stack at STARTSEG:STARTOFS

  cli                                  ; Disable interrupts
  mov  ax, STARTSEG                    ; AX = STARTSEG
  mov  ss, ax                          ; Stack segment is STARTSEG
  mov  sp, STARTOFS                    ; Stack pointer is STARTOFS
  sti                                  ; Enable interrupts

; Save the drive we booted from

  mov  BootDrive, dl

;--------------------------------------------------------------------------

  call DetectCPU

;--------------------------------------------------------------------------

ReadSuperBlock :

; Reset disk controller
  xor  ax, ax
  int  0x13
  jc   Reboot

; Load EXOS SuperBlock - Disk location : 0x0400, Size : 0x0400
; Load it at 9000:0000
; INT 0x13 wants buffer in ES:BX
; DL already contains the boot drive number

  mov  ax, SUPERSEG
  mov  es, ax
  mov  bx, SUPEROFS

  mov  ah, 2                           ; Load 1 block (2 sectors)
  mov  al, 2                           ; SuperBlock = 2 sectors
  mov  ch, 0                           ; Cylinder 0
  mov  cl, 3                           ; Sector 3 (Sectors begin at 1)
  mov  dh, 0                           ; Head 0
  mov  dl, BootDrive                   ; Drive saved earlier
  int  0x13
  jc   ReadSuperBlock                  ; If error do it again

;--------------------------------------------------------------------------

CheckMagic :

; EXOS magic number is first dword of SuperBlock

  push ds                              ; Save DS

  mov  ax, STARTSEG
  mov  ds, ax
  mov  si, offset EXOSMagic

  mov  ax, SUPERSEG
  mov  es, ax
  mov  di, 0

  cld

; Compare bytes 0 & 1
  cmpsw
  jnz  Reboot

; Compare bytes 2 & 3
  cmpsw
  jnz  Reboot

  pop  ds                              ; Restore DS

;--------------------------------------------------------------------------

; Load Kernel


;--------------------------------------------------------------------------

; Set A20 line
  cli
  xor  cx, cx

ClearBuffer1 :

  in   al, 0x64                        ; Get input from keyboard status port
  test al, 0x02                        ; Test the buffer full flag
  loopnz ClearBuffer1                  ; Loop until buffer is empty
  mov  al, 0xD1                        ; Write to output port
  out  0x64, al                        ; Output command to keyboard

ClearBuffer2 :

  in   al, 0x64
  test al, 0x02
  loopnz ClearBuffer2
  mov  al, 0xDF
  out  0x60, al

  mov  cx, 0x14

WaitKB :

  out  0xED, ax
  loop WaitKB

  sti

;--------------------------------------------------------------------------

; Remaining data of boot sector

Fill :

  db ((SECTORSIZE - 2) - ($ - _Sector0)) dup (0)

;--------------------------------------------------------------------------

; Magic number for BIOS
; Must be present if we want the BIOS to load our boot sector

BIOSMagic :

  dw 0xAA55

;--------------------------------------------------------------------------

BootSector ends

;--------------------------------------------------------------------------

end

