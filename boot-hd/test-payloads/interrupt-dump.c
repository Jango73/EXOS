/************************************************************************\

    EXOS Interrupt Dump Payload
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Interrupt dump payload (PIC + Local APIC)

\************************************************************************/

// I386 16-bit real mode payload for interrupt diagnostics

#include "../../kernel/include/arch/i386/i386.h"
#include "../../kernel/include/CoreString.h"
#include "../../kernel/include/drivers/LocalAPIC.h"
#include "../include/vbr-realmode-utils.h"

/************************************************************************/

__asm__(".code16gcc");

/************************************************************************/

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40

#define KEYBOARD_BIOS_INT 0x16
#define SCREEN_LINES 25
#define SCREEN_RESERVED_LINES 3
#define SCREEN_LINES_BEFORE_PAUSE (SCREEN_LINES - SCREEN_RESERVED_LINES)

/************************************************************************/

extern void EnterUnrealMode(void);
extern void LeaveUnrealMode(void);

/************************************************************************/

typedef struct tag_OUTPUT_CONTEXT {
    STR TemporaryString[128];
} OUTPUT_CONTEXT, *LPOUTPUT_CONTEXT;

/************************************************************************/

/**
 * @brief Output a single character to the BIOS TTY.
 * @param Character ASCII character to output.
 */
static U8 LinesOnScreen = 0;
static BOOL PauseActive = FALSE;

static void OutputChar(U8 Character) {
    __asm__ __volatile__(
        "mov   $0x0E, %%ah\n\t"
        "mov   %0, %%al\n\t"
        "int   $0x10\n\t"
        :
        : "r"(Character)
        : "ah", "al");

    if (Character == '\n') {
        LinesOnScreen++;
    }
}

/************************************************************************/

/**
 * @brief Output a zero-terminated string.
 * @param String Text to output.
 */
static void WriteString(LPCSTR String) {
    if (PauseActive) {
        return;
    }

    if (LinesOnScreen >= SCREEN_LINES_BEFORE_PAUSE) {
        PauseActive = TRUE;
        WriteString(TEXT("-- more -- (SPACE/ENTER, ESC to halt)\r\n"));
        while (TRUE) {
            U16 Key;
            U8 Character;

            __asm__ __volatile__(
                "xor %%ah, %%ah\n\t"
                "int $0x16\n\t"
                : "=a"(Key)
                :
                : "cc");

            Character = (U8)(Key & 0xFF);
            if (Character == 27 || Character == 'q' || Character == 'Q') {
                Hang();
            }
            if (Character == ' ' || Character == '\r' || Character == '\n') {
                break;
            }
        }
        LinesOnScreen = 0;
        PauseActive = FALSE;
    }

    while (*String) {
        OutputChar((U8)*String++);
    }
}

/************************************************************************/

/**
 * @brief Write formatted output using a temporary buffer.
 * @param Context Output context with buffer.
 * @param Format Format string.
 */
static void WriteFormat(LPOUTPUT_CONTEXT Context, LPCSTR Format, ...) {
    VarArgList Arguments;

    VarArgStart(Arguments, Format);
    StringPrintFormatArgs(Context->TemporaryString, Format, Arguments);
    VarArgEnd(Arguments);

    WriteString(Context->TemporaryString);
}

/************************************************************************/

/**
 * @brief Read a byte from an I/O port.
 * @param Port I/O port address.
 * @return Byte read.
 */
static U8 InPortByte(U16 Port) {
    U8 Value;
    __asm__ __volatile__("inb %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

/************************************************************************/

/**
 * @brief Write a byte to an I/O port.
 * @param Port I/O port address.
 * @param Value Byte to write.
 */
static void OutPortByte(U16 Port, U8 Value) {
    __asm__ __volatile__("outb %0, %1" ::"a"(Value), "Nd"(Port));
}

/************************************************************************/

/**
 * @brief Check whether a key is available in BIOS keyboard buffer.
 * @return TRUE if a key is available.
 */
/**
 * @brief Read PIC register (IRR/ISR) using OCW3 command.
 * @param CommandPort PIC command port.
 * @param Command OCW3 command value.
 * @return Register value.
 */
static U8 ReadPICRegister(U16 CommandPort, U8 Command) {
    OutPortByte(CommandPort, Command);
    return InPortByte(CommandPort);
}

/************************************************************************/

/**
 * @brief Read the current PIT counter value.
 * @return Current counter value.
 */
static U16 ReadPITCounter0(void) {
    OutPortByte(PIT_COMMAND, 0x00);
    U8 Low = InPortByte(PIT_CHANNEL0);
    U8 High = InPortByte(PIT_CHANNEL0);
    return (U16)((U16)Low | ((U16)High << 8));
}

/************************************************************************/

/**
 * @brief Read MSR value.
 * @param Index MSR index.
 * @param Low Output low 32-bit value.
 * @param High Output high 32-bit value.
 */
static void ReadMsr(U32 Index, U32* Low, U32* High) {
    U32 LocalLow = 0;
    U32 LocalHigh = 0;
    __asm__ __volatile__("rdmsr" : "=a"(LocalLow), "=d"(LocalHigh) : "c"(Index));
    if (Low != NULL) {
        *Low = LocalLow;
    }
    if (High != NULL) {
        *High = LocalHigh;
    }
}

/************************************************************************/

/**
 * @brief Read a 32-bit value from a linear address using unreal mode.
 * @param Address Linear address.
 * @return 32-bit value.
 */
static U32 ReadLinearU32(U32 Address) {
    U32 Value;
    EnterUnrealMode();
    __asm__ __volatile__("movl (%1), %0" : "=r"(Value) : "r"(Address) : "memory");
    LeaveUnrealMode();
    return Value;
}

/************************************************************************/

/**
 * @brief Dump PIC registers and masks.
 * @param Context Output context.
 */
static void DumpPICState(LPOUTPUT_CONTEXT Context) {
    U8 Mask1 = InPortByte(PIC1_DATA);
    U8 Mask2 = InPortByte(PIC2_DATA);
    U8 Irr1 = ReadPICRegister(PIC1_COMMAND, 0x0A);
    U8 Irr2 = ReadPICRegister(PIC2_COMMAND, 0x0A);
    U8 Isr1 = ReadPICRegister(PIC1_COMMAND, 0x0B);
    U8 Isr2 = ReadPICRegister(PIC2_COMMAND, 0x0B);

    WriteFormat(Context, TEXT("[PIC] Mask1=%x Mask2=%x\r\n"), Mask1, Mask2);
    WriteFormat(Context, TEXT("[PIC] IRR1=%x IRR2=%x\r\n"), Irr1, Irr2);
    WriteFormat(Context, TEXT("[PIC] ISR1=%x ISR2=%x\r\n"), Isr1, Isr2);
}

/************************************************************************/

/**
 * @brief Dump IMCR state.
 * @param Context Output context.
 */
static void DumpIMCRState(LPOUTPUT_CONTEXT Context) {
    OutPortByte(0x22, 0x70);
    U8 Value = InPortByte(0x23);
    WriteFormat(Context, TEXT("[IMCR] Value=%x\r\n"), Value);
}

/************************************************************************/

/**
 * @brief Dump Local APIC registers if enabled.
 * @param Context Output context.
 */
static void DumpLocalApicState(LPOUTPUT_CONTEXT Context) {
    U32 ApicBaseLow;
    U32 ApicBaseHigh;

    ReadMsr(IA32_APIC_BASE_MSR, &ApicBaseLow, &ApicBaseHigh);

    U32 BaseAddress = ApicBaseLow & IA32_APIC_BASE_ADDR_MASK;
    BOOL Enabled = (ApicBaseLow & IA32_APIC_BASE_ENABLE) != 0U;
    BOOL Bsp = (ApicBaseLow & IA32_APIC_BASE_BSP) != 0U;

    WriteFormat(Context, TEXT("[LocalAPIC] MSR=%x Base=%x Enabled=%u BSP=%u\r\n"),
        ApicBaseLow, BaseAddress, Enabled ? 1U : 0U, Bsp ? 1U : 0U);

    if (!Enabled) {
        return;
    }

    U32 Spurious = ReadLinearU32(BaseAddress + LOCAL_APIC_SPURIOUS_IV);
    U32 TaskPriority = ReadLinearU32(BaseAddress + LOCAL_APIC_TPR);
    U32 Lint0 = ReadLinearU32(BaseAddress + LOCAL_APIC_LVT_LINT0);
    U32 IsrVector32 = ReadLinearU32(BaseAddress + LOCAL_APIC_ISR_BASE + 0x10);
    U32 IrrVector32 = ReadLinearU32(BaseAddress + LOCAL_APIC_IRR_BASE + 0x10);

    WriteFormat(Context, TEXT("[LocalAPIC] SVR=%x TPR=%x\r\n"), Spurious, TaskPriority);
    WriteFormat(Context, TEXT("[LocalAPIC] LINT0=%x\r\n"), Lint0);
    WriteFormat(Context, TEXT("[LocalAPIC] ISR[32]=%x IRR[32]=%x\r\n"), IsrVector32, IrrVector32);
}

/************************************************************************/

/**
 * @brief Dump PIT counter state.
 * @param Context Output context.
 */
static void DumpPitState(LPOUTPUT_CONTEXT Context) {
    U16 Counter = ReadPITCounter0();
    WriteFormat(Context, TEXT("[PIT] Counter=%u\r\n"), Counter);
}

/************************************************************************/

/**
 * @brief Busy delay to avoid flooding output.
 */
static void ShortDelay(void) {
    volatile UINT Spin = 0;
    for (Spin = 0; Spin < 2000000; Spin++) {
        __asm__ __volatile__("");
    }
}

/************************************************************************/

/**
 * @brief Entry point for the boot payload.
 * @param BootDrive Boot drive number.
 * @param PartitionLba Partition LBA.
 */
void BootMain(U32 BootDrive, U32 PartitionLba) {
    UNUSED(BootDrive);
    UNUSED(PartitionLba);

    OUTPUT_CONTEXT Context;

    WriteString(TEXT("\r\n"));
    WriteString(TEXT("*********************************************\r\n"));
    WriteString(TEXT("*    EXOS Interrupt Dump Payload            *\r\n"));
    WriteString(TEXT("*********************************************\r\n"));
    WriteString(TEXT("\r\n"));
    WriteString(TEXT("Auto pause like more when the screen fills.\r\n"));
    WriteString(TEXT("Controls: SPACE/ENTER continue, ESC halt.\r\n"));
    WriteString(TEXT("\r\n"));

    EnableA20();

    while (TRUE) {
        WriteString(TEXT("------------------------------------------------\r\n"));
        DumpPICState(&Context);
        DumpIMCRState(&Context);
        DumpPitState(&Context);
        DumpLocalApicState(&Context);
        ShortDelay();
    }

    WriteString(TEXT("[InterruptDump] Halting.\r\n"));
    Hang();
}

/************************************************************************/
