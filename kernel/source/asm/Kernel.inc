
;-------------------------------------------------------------------------
;
;   EXOS Kernel
;   Copyright (c) 1999-2025 Jango73
;
;   This program is free software: you can redistribute it and/or modify
;   it under the terms of the GNU General Public License as published by
;   the Free Software Foundation, either version 3 of the License, or
;   (at your option) any later version.
;
;   This program is distributed in the hope that it will be useful,
;   but WITHOUT ANY WARRANTY; without even the implied warranty of
;   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;   GNU General Public License for more details.
;
;   You should have received a copy of the GNU General Public License
;   along with this program.  If not, see <https://www.gnu.org/licenses/>.
;
;
;   Assembly system definitions
;
;-------------------------------------------------------------------------

;-------------------------------------------------------------------------
; Useful constants

MAX_UINT    equ 0xFFFFFFFF
U32_SIZE    equ 4

N_1KB       equ 0x00000400
N_2KB       equ 0x00000800
N_4KB       equ 0x00001000
N_8KB       equ 0x00002000
N_16KB      equ 0x00004000
N_32KB      equ 0x00008000
N_64KB      equ 0x00010000
N_128KB     equ 0x00020000
N_256KB     equ 0x00040000
N_512KB     equ 0x00080000
N_1MB       equ 0x00100000
N_2MB       equ 0x00200000
N_4MB       equ 0x00400000
N_8MB       equ 0x00800000

;-------------------------------------------------------------------------
; Shift values to multiply or divide by machine constants

MUL_2       equ 1
MUL_4       equ 2
MUL_8       equ 3
MUL_16      equ 4
MUL_32      equ 5
MUL_64      equ 6
MUL_128     equ 7
MUL_256     equ 8
MUL_512     equ 9
MUL_1KB     equ 10
MUL_2KB     equ 11
MUL_4KB     equ 12
MUL_8KB     equ 13
MUL_16KB    equ 14
MUL_32KB    equ 15
MUL_64KB    equ 16
MUL_128KB   equ 17
MUL_256KB   equ 18
MUL_512KB   equ 19
MUL_1MB     equ 20
MUL_2MB     equ 21
MUL_4MB     equ 22
MUL_8MB     equ 23
MUL_16MB    equ 24

;-------------------------------------------------------------------------
; Bit layout of the EFlags register

EFLAGS_CF    equ 0x00000001
EFLAGS_A1    equ 0x00000002
EFLAGS_PF    equ 0x00000004
EFLAGS_AF    equ 0x00000010
EFLAGS_ZF    equ 0x00000040
EFLAGS_SF    equ 0x00000080
EFLAGS_TF    equ 0x00000100
EFLAGS_IF    equ 0x00000200
EFLAGS_OF    equ 0x00000800
EFLAGS_IOPL1 equ 0x00001000
EFLAGS_IOPL2 equ 0x00002000
EFLAGS_NT    equ 0x00004000
EFLAGS_RF    equ 0x00010000
EFLAGS_VM    equ 0x00020000

;-------------------------------------------------------------------------
; Bit layout of CR0

CR0_PROTECTED_MODE equ 0x00000001
CR0_COPROCESSOR   equ 0x00000002
CR0_MONITOR_COPROCESSOR  equ 0x00000004
CR0_TASKSWITCH    equ 0x00000008
CR0_80387         equ 0x00000010
CR0_PAGING        equ 0x80000000

;-------------------------------------------------------------------------

PAGE_MASK          equ 0xFFFFF000
PAGE_TABLE_SIZE    equ N_4KB

PAGE_BIT_PRESENT   equ 0x00000001
PAGE_BIT_RDWR      equ 0x00000002
PAGE_BIT_PRIVILEGE equ 0x00000004
PAGE_BIT_THROUGH   equ 0x00000008
PAGE_BIT_NOCACHE   equ 0x00000010
PAGE_BIT_ACCESSED  equ 0x00000020
PAGE_BIT_DIRTY     equ 0x00000040
PAGE_BIT_RESERVED  equ 0x00000080
PAGE_BIT_GLOBAL    equ 0x00000100
PAGE_BIT_USER1     equ 0x00000200
PAGE_BIT_USER2     equ 0x00000400
PAGE_BIT_FIXED     equ 0x00000800

PAGE_BIT_SYSTEM    equ (PAGE_BIT_PRESENT | PAGE_BIT_RDWR | PAGE_BIT_FIXED)

PAGE_SIZE_MUL      equ MUL_4KB

;-------------------------------------------------------------------------
; Physical and virtual memory addresses

IDT_SIZE     equ N_4KB
GDT_SIZE     equ N_8KB

VMA_USER      equ 0x00400000
VMA_LIBRARY   equ 0xA0000000
VMA_KERNEL    equ 0xC0000000

LOW_MEMORY_PAGE_1   equ 0x1000  ; Reserved by VBR system structures
LOW_MEMORY_PAGE_2   equ 0x2000  ; Reserved by VBR system structures
LOW_MEMORY_PAGE_3   equ 0x3000  ; Reserved by VBR system structures
LOW_MEMORY_PAGE_4   equ 0x4000
LOW_MEMORY_PAGE_5   equ 0x5000  ; RMC code base
LOW_MEMORY_PAGE_6   equ 0x6000  ; RMC buffers
LOW_MEMORY_PAGE_7   equ 0x7000
LOW_MEMORY_PAGE_8   equ 0x8000

;-------------------------------------------------------------------------
; Values related to IDT, GDT, LDT

SEGMENT_DESCRIPTOR_SIZE equ 8
GATE_DESCRIPTOR_SIZE    equ 8

NUM_INTS                equ 256

;-------------------------------------------------------------------------
; Privilege levels

PRIVILEGE_KERNEL   equ 0x00
PRIVILEGE_DRIVERS  equ 0x01
PRIVILEGE_ROUTINES equ 0x02
PRIVILEGE_USER     equ 0x03

;-------------------------------------------------------------------------
; Selector values

SELECTOR_GLOBAL equ 0x00
SELECTOR_LOCAL  equ 0x04

;-------------------------------------------------------------------------
; Kernel selectors

SELECTOR_NULL        equ 0x00
SELECTOR_KERNEL_CODE equ (0x08 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_KERNEL_DATA equ (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_USER_CODE   equ (0x18 | SELECTOR_GLOBAL | PRIVILEGE_USER)
SELECTOR_USER_DATA   equ (0x20 | SELECTOR_GLOBAL | PRIVILEGE_USER)
SELECTOR_REAL_CODE   equ (0x28 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_REAL_DATA   equ (0x30 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

;-------------------------------------------------------------------------

DESC_OFFSET_BASE_00_15 equ 0x02
DESC_OFFSET_BASE_16_23 equ 0x04
DESC_OFFSET_BASE_24_31 equ 0x07

GATE_OFFSET_OFFSET_00_15 equ 0x02
GATE_OFFSET_OFFSET_16_31 equ 0x06

;-------------------------------------------------------------------------
; Values related to IRQs and the PIC(S) (8259-1 & 8259-2)

INTERRUPT_CONTROL equ 0x0020
INTERRUPT_DONE    equ 0x0020

PIC1_CMD          equ 0x0020           ; PIC1 command port
PIC1_DATA         equ 0x0021           ; PIC1 data port
PIC2_CMD          equ 0x00A0           ; PIC2 command port
PIC2_DATA         equ 0x00A1           ; PIC2 data port
ICW1_ICW4         equ 0x0001           ; ICW4 (not) needed
ICW1_SINGLE       equ 0x0002           ; Single (cascade) mode
ICW1_INTERVAL4    equ 0x0004           ; Call address interval 4 (8)
ICW1_LEVEL        equ 0x0008           ; Level triggered (edge) mode
ICW1_INIT         equ 0x0010           ; Initialization - r ired!
ICW4_8086         equ 0x0001           ; 8086/88 (MCS-80/85) mode
ICW4_AUTO         equ 0x0002           ; Auto (normal) EOI
ICW4_BUF_SLAVE    equ 0x0008           ; Buffered mode/slave
ICW4_BUF_MASTER   equ 0x000C           ; Buffered mode/master
ICW4_SFNM         equ 0x0010           ; Special fully nested (not)

PIC_EOI           equ 0x0020           ; End Of Interrupt
PIC1_VECTOR       equ 0x0020           ; New PIC interrupt vectors
PIC2_VECTOR       equ 0x0028

;-------------------------------------------------------------------------
; Values related to the 8253 chip (timer)

CLOCK_DATA    equ 0x0040
CLOCK_COMMAND equ 0x0043

CLOCK_18_2 equ 0xFFFF
CLOCK_100  equ 0x2E9C

;-------------------------------------------------------------------------
; Values related to the 8042 chip (keyboard)

KEYBOARD_CONTROL equ 0x0060
KEYBOARD_STATUS  equ 0x0064

KEYBOARD_STATUS_OUT_FULL     equ 0x01
KEYBOARD_STATUS_IN_FULL      equ 0x02
KEYBOARD_STATUS_COMMAND      equ 0x08
KEYBOARD_STATUS_ACTIVE       equ 0x10
KEYBOARD_STATUS_OUT_ERROR    equ 0x20
KEYBOARD_STATUS_IN_ERROR     equ 0x40
KEYBOARD_STATUS_PARITY_ERROR equ 0x80

;-------------------------------------------------------------------------
; Log values
LOG_DEBUG equ 0x0001
LOG_VERBOSE equ 0x0002
LOG_WARNING equ 0x0004
LOG_ERROR equ 0x0008

;-------------------------------------------------------------------------
; KernelStartupInfo data layout (partial)

STRUC KernelStartupInfo
    .StubAddress        RESB 4
    .StackTop           RESB 4
    .PageDirectory      RESB 4
    .IRQMask_21_PM      RESB 4
    .IRQMask_A1_PM      RESB 4
    .IRQMask_21_RM      RESB 4
    .IRQMask_A1_RM      RESB 4
    .ConsoleX           RESB 4
    .ConsoleY           RESB 4
    .MemorySize         RESB 4
    .PageCount          RESB 4
    .E820_Count         RESB 4
ENDSTRUC

;-------------------------------------------------------------------------
; Kernel i386 data layout - used to fetch GDT/IDT pointers from C
; Minimal mapping: first fields must match C struct order.
; Keep this in sync with the C definition of 'Kernel_i386'.

STRUC KERNELDATA_I386
    .IDT                RESB 4        ; LPSEGMENTDESCRIPTOR*
    .GDT                RESB 4        ; LPSEGMENTDESCRIPTOR*
    .TSS                RESB 4        ; LPTASKSTATESEGMENT*
    .PPB                RESB 4        ; LPPAGEBITMAP*
ENDSTRUC

;-------------------------------------------------------------------------
; Interrupt frame - MUST be synced with INTERRUPTFRAME in I386.h

STRUC INTERRUPT_FRAME
    .EFlags             RESB 4
    .EAX                RESB 4
    .EBX                RESB 4
    .ECX                RESB 4
    .EDX                RESB 4
    .ESI                RESB 4
    .EDI                RESB 4
    .ESP                RESB 4
    .EBP                RESB 4
    .EIP                RESB 4
    .CS                 RESB 4
    .DS                 RESB 4
    .SS                 RESB 4
    .ES                 RESB 4
    .FS                 RESB 4
    .GS                 RESB 4
    .CR0                RESB 4
    .CR2                RESB 4
    .CR3                RESB 4
    .CR4                RESB 4
    .DR0                RESB 4
    .DR1                RESB 4
    .DR2                RESB 4
    .DR3                RESB 4
    .DR4                RESB 4
    .DR5                RESB 4
    .DR6                RESB 4
    .DR7                RESB 4
    .SS0                RESB 4      ; SS in ring 0
    .ESP0               RESB 4      ; ESP in ring 0
    .IntNo              RESB 4      ; Interrupt / exception vector
    .ErrCode            RESB 4      ; CPU error code (0 for #UD)
ENDSTRUC

;-------------------------------------------------------------------------
; Macros

%macro FUNC_HEADER 0
    align 16
%endmacro

;-------------------------------------------------------------------------
; Extern C symbols

extern KernelMain
extern InitializeKernel
extern KernelPrint
extern ConsolePrint
extern ConsolePrintChar
extern ClockTestTask
extern KernelLogText

extern SystemCallHandler
extern DriverCallHandler

extern DefaultHandler
extern DivideErrorHandler
extern DebugExceptionHandler
extern NMIHandler
extern BreakPointHandler
extern OverflowHandler
extern BoundRangeHandler
extern InvalidOpcodeHandler
extern DeviceNotAvailHandler
extern DoubleFaultHandler
extern MathOverflowHandler
extern InvalidTSSHandler
extern SegmentFaultHandler
extern StackFaultHandler
extern GeneralProtectionHandler
extern PageFaultHandler
extern AlignmentCheckHandler
extern MachineCheckHandler
extern FloatingPointHandler

extern ClockHandler
extern KeyboardHandler
extern PIC2Handler
extern COM2Handler
extern COM1Handler
extern RTCHandler
extern PCIHandler
extern MouseHandler
extern FPUHandler
extern HardDriveHandler
extern Scheduler

extern Sleep

extern Kernel
extern KernelStartup
extern __stub_init_end
extern __stub_init_start
extern __bss_init_end
extern __bss_init_start

;----------------------------------------------------------
