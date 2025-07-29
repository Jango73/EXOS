
; Kernel.inc

;----------------------------------------------------------
; Useful constants

N_1KB   equ 0x00000400
N_2KB   equ 0x00000800
N_4KB   equ 0x00001000
N_8KB   equ 0x00002000
N_16KB  equ 0x00004000
N_32KB  equ 0x00008000
N_64KB  equ 0x00010000
N_128KB equ 0x00020000
N_256KB equ 0x00040000
N_512KB equ 0x00080000
N_1MB   equ 0x00100000
N_2MB   equ 0x00200000
N_4MB   equ 0x00400000
N_8MB   equ 0x00800000

;----------------------------------------------------------
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

;----------------------------------------------------------
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

;----------------------------------------------------------
; Bit layout of CR0

CR0_PROTECTEDMODE equ 0x00000001
CR0_COPROCESSOR   equ 0x00000002
CR0_MONITORCOPRO  equ 0x00000004
CR0_TASKSWITCH    equ 0x00000008
CR0_80387         equ 0x00000010
CR0_PAGING        equ 0x80000000

;----------------------------------------------------------

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

;----------------------------------------------------------
; Physical and virtual memory addresses

LOW_SIZE     equ N_1MB
HMA_SIZE     equ N_128KB
IDT_SIZE     equ N_4KB
GDT_SIZE     equ N_8KB
PGD_SIZE     equ PAGE_TABLE_SIZE
PGS_SIZE     equ PAGE_TABLE_SIZE
PGK_SIZE     equ PAGE_TABLE_SIZE
PGL_SIZE     equ PAGE_TABLE_SIZE
PGH_SIZE     equ PAGE_TABLE_SIZE
TSS_SIZE     equ N_32KB
PPB_SIZE     equ N_64KB

SYSTEM_SIZE  equ (IDT_SIZE + GDT_SIZE + PGD_SIZE + PGS_SIZE + PGK_SIZE + PGL_SIZE + PGH_SIZE + TSS_SIZE + PPB_SIZE)

KERNEL_SIZE  equ N_64KB + N_64KB

PA_LOW       equ 0x00000000
PA_HMA       equ (PA_LOW + LOW_SIZE)
PA_IDT       equ (PA_HMA + HMA_SIZE)
PA_GDT       equ (PA_IDT + IDT_SIZE)
PA_PGD       equ (PA_GDT + GDT_SIZE)
PA_PGS       equ (PA_PGD + PGD_SIZE)
PA_PGK       equ (PA_PGS + PGS_SIZE)
PA_PGL       equ (PA_PGK + PGK_SIZE)
PA_PGH       equ (PA_PGL + PGL_SIZE)
PA_TSS       equ (PA_PGH + PGH_SIZE)
PA_PPB       equ (PA_TSS + TSS_SIZE)
PA_KERNEL    equ (PA_PPB + PPB_SIZE)

PA_SYSTEM    equ PA_IDT

LA_USER      equ 0x00400000
LA_KERNEL    equ 0xC0000000
LA_SYSTEM    equ 0xFF400000
LA_DIRECTORY equ 0xFF800000

LA_IDT       equ LA_SYSTEM
LA_GDT       equ (LA_IDT + IDT_SIZE)

;----------------------------------------------------------
; Values related to IDT, GDT, LDT

SEGMENT_DESCRIPTOR_SIZE equ 8
GATE_DESCRIPTOR_SIZE    equ 8

NUM_INTS                equ 256
NUM_DESCRIPTORS         equ 8
NUM_TASKS               equ 128
NUM_LOCAL_DESCRIPTORS   equ 4

;----------------------------------------------------------
; Privilege levels

PRIVILEGE_KERNEL   equ 0x00
PRIVILEGE_DRIVERS  equ 0x01
PRIVILEGE_ROUTINES equ 0x02
PRIVILEGE_USER     equ 0x03

;----------------------------------------------------------
; Selector values

SELECTOR_GLOBAL equ 0x00
SELECTOR_LOCAL  equ 0x04

;----------------------------------------------------------
; Kernel selectors

SELECTOR_NULL        equ 0x00
SELECTOR_UNUSED      equ 0x08
SELECTOR_KERNEL_CODE equ (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_KERNEL_DATA equ (0x18 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_USER_CODE   equ (0x20 | SELECTOR_GLOBAL | PRIVILEGE_USER)
SELECTOR_USER_DATA   equ (0x28 | SELECTOR_GLOBAL | PRIVILEGE_USER)
SELECTOR_REAL_CODE   equ (0x30 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_REAL_DATA   equ (0x38 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_TSS_0       equ (0x40 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_LDT_0       equ (0x48 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_TSS_1       equ (0x50 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_LDT_1       equ (0x58 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

;----------------------------------------------------------

DESC_OFFSET_BASE_00_15 equ 0x02
DESC_OFFSET_BASE_16_23 equ 0x04
DESC_OFFSET_BASE_24_31 equ 0x07

GATE_OFFSET_OFFSET_00_15 equ 0x02
GATE_OFFSET_OFFSET_16_31 equ 0x06

;----------------------------------------------------------
; Offsets of values stored by the stub

INFO_ORG_CS           equ 0x0000
INFO_ORG_DS           equ 0x0002
INFO_ORG_ES           equ 0x0004
INFO_ORG_SS           equ 0x0006
INFO_ORG_FS           equ 0x0008
INFO_ORG_GS           equ 0x000A
INFO_GDT              equ 0x0010
INFO_IDT              equ 0x0014
INFO_MEM              equ 0x0018
INFO_KERNEL_CODE      equ 0x0020
INFO_KERNEL_DATA      equ 0x0024
INFO_CONSOLE_WIDTH    equ 0x0030
INFO_CONSOLE_HEIGHT   equ 0x0034
INFO_CONSOLE_CURSOR_X equ 0x0038
INFO_CONSOLE_CURSOR_Y equ 0x003C

;----------------------------------------------------------
; Values related to BIOS

BIOS_CONSOLE_CURSOR equ ((0x0040 << 4) | 0x0050)

;----------------------------------------------------------
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

;----------------------------------------------------------
; Values related to the 8253 chip (timer)

CLOCK_DATA    equ 0x0040
CLOCK_COMMAND equ 0x0043

CLOCK_18_2 equ 0xFFFF
CLOCK_100  equ 0x2E9C

;----------------------------------------------------------
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

;----------------------------------------------------------
; Extern C functions

extern KernelMain
extern InitializeKernel
extern KernelPrint
extern ConsolePrint
extern ConsolePrintChar
extern ClockTask
extern KillTask
extern GetCurrentTask

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

extern ClockHandler
extern KeyboardHandler
extern MouseHandler
extern HardDriveHandler

extern Kernel
extern Text_Zob
extern StubAddress

;----------------------------------------------------------
