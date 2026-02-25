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


    Interrupt dump payload (paged)

\************************************************************************/

// X86_32 16-bit real mode payload for interrupt diagnostics

#include "../../kernel/include/arch/x86-32/x86-32.h"
#include "../../kernel/include/CoreString.h"
#include "../../kernel/include/drivers/interrupts/LocalAPIC.h"
#include "../include/vbr-realmode-utils.h"
#include "dumper.h"

/************************************************************************/

__asm__(".code16gcc");

/************************************************************************/

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40

#define PAGE_COUNT 9

/************************************************************************/

/**
 * @brief Output a single character to the BIOS TTY.
 * @param Character ASCII character to output.
 */
static void OutputChar(U8 Char) {
    __asm__ __volatile__(
        "mov   $0x0E, %%ah\n\t"
        "mov   %0, %%al\n\t"
        "int   $0x10\n\t"
        :
        : "r"(Char)
        : "ah", "al");
}

/************************************************************************/

/**
 * @brief Reset output buffer state.
 * @param Context Output context.
 */
static void OutputBufferReset(LPOUTPUT_CONTEXT Context) {
    Context->BufferLength = 0;
    Context->LineCount = 1;
    Context->LineOffsets[0] = 0;
}

/************************************************************************/

static void OutputBufferAppendChar(LPOUTPUT_CONTEXT Context, U8 Char) {
    if (Context->BufferLength + 1 >= OUTPUT_BUFFER_SIZE) {
        return;
    }

    Context->Buffer[Context->BufferLength] = (STR)Char;
    Context->BufferLength++;

    if (Char == '\n' && Context->LineCount < OUTPUT_MAX_LINES) {
        Context->LineOffsets[Context->LineCount] = Context->BufferLength;
        Context->LineCount++;
    }
}

/************************************************************************/

/**
 * @brief Output a zero-terminated string into the buffer.
 * @param Context Output context.
 * @param String Text to output.
 */
static void WriteString(LPOUTPUT_CONTEXT Context, LPCSTR String) {
    while (*String) {
        OutputBufferAppendChar(Context, (U8)*String++);
    }
}

/************************************************************************/

/**
 * @brief Write formatted output using a temporary buffer.
 * @param Context Output context with buffer.
 * @param Format Format string.
 */
void WriteFormatRaw(LPOUTPUT_CONTEXT Context, LPCSTR Format, ...) {
    VarArgList Arguments;

    VarArgStart(Arguments, Format);
    StringPrintFormatArgs(Context->TemporaryString, Format, Arguments);
    VarArgEnd(Arguments);

    WriteString(Context, Context->TemporaryString);
}

/************************************************************************/

static void WritePadding(LPOUTPUT_CONTEXT Context, UINT Count) {
    for (UINT Index = 0; Index < Count; Index++) {
        OutputBufferAppendChar(Context, ' ');
    }
}

/************************************************************************/

/**
 * @brief Write formatted output with aligned value column.
 * @param Context Output context with buffer.
 * @param ValueColumn Column where values should start.
 * @param Label Field label.
 * @param ValueFormat Value format string.
 */
void WriteFormat(LPOUTPUT_CONTEXT Context, UINT ValueColumn, LPCSTR Label, LPCSTR ValueFormat, ...) {
    VarArgList Arguments;
    UINT LabelLength = StringLength(Label);
    UINT Padding = 1;

    WriteString(Context, Label);
    if (ValueColumn > LabelLength) {
        Padding = ValueColumn - LabelLength;
    }
    WritePadding(Context, Padding);

    VarArgStart(Arguments, ValueFormat);
    StringPrintFormatArgs(Context->TemporaryString, ValueFormat, Arguments);
    VarArgEnd(Arguments);

    WriteString(Context, Context->TemporaryString);
}

/************************************************************************/

/**
 * @brief Read a byte from an I/O port.
 * @param Port I/O port address.
 * @return Byte read.
 */
static U8 InPortByte(U16 Port) {
    return BootInPortByte((U32)Port);
}

/************************************************************************/

/**
 * @brief Write a byte to an I/O port.
 * @param Port I/O port address.
 * @param Value Byte to write.
 */
static void OutPortByte(U16 Port, U8 Value) {
    BootOutPortByte((U32)Port, (U32)Value);
}

/************************************************************************/

/**
 * @brief Enable A20 using the fast port 0x92 method.
 */
void EnableA20Fast(void) {
    U8 Value = InPortByte(0x92);
    Value = (U8)(Value | 0x02);
    OutPortByte(0x92, Value);
}

/************************************************************************/

/**
 * @brief Disable A20 using the fast port 0x92 method.
 */
void DisableA20Fast(void) {
    U8 Value = InPortByte(0x92);
    Value = (U8)(Value & (U8)~0x02);
    OutPortByte(0x92, Value);
}

/************************************************************************/

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
 * @brief Read PIT status for channel 0.
 * @return PIT status byte.
 */
static U8 ReadPITStatus0(void) {
    OutPortByte(PIT_COMMAND, 0xE2);
    return InPortByte(PIT_CHANNEL0);
}

/************************************************************************/

/**
 * @brief Read a byte from a linear address using unreal mode.
 * @param Address Linear address.
 * @return Byte value.
 */
static U8 ReadLinearU8(U32 Address) {
    return BootReadLinearU8(Address);
}

/************************************************************************/

/**
 * @brief Read a 32-bit value from a linear address using unreal mode.
 * @param Address Linear address.
 * @return 32-bit value.
 */
static U32 ReadLinearU32(U32 Address) {
    return BootReadLinearU32(Address);
}

/************************************************************************/

/**
 * @brief Write a 32-bit value to a linear address using unreal mode.
 * @param Address Linear address.
 * @param Value Value to write.
 */
static void WriteLinearU32(U32 Address, U32 Value) {
    BootWriteLinearU32(Address, Value);
}

/************************************************************************/

/**
 * @brief Read an I/O APIC register.
 * @param Base IO APIC base address.
 * @param Register Register index.
 * @return Register value.
 */
U32 ReadIOApicRegister(U32 Base, U8 Register) {
    WriteLinearU32(Base + IOAPIC_REGSEL, Register);
    return ReadLinearU32(Base + IOAPIC_IOWIN);
}

/************************************************************************/

void CopyFromLinear(U32 Address, void* Destination, U32 Size) {
    U8* Out = (U8*)Destination;
    for (U32 Index = 0; Index < Size; Index++) {
        Out[Index] = ReadLinearU8(Address + Index);
    }
}

/************************************************************************/

static UINT GetScreenRows(void) {
    U8 RowsMinus1 = 0;

    CopyFromLinear(0x484, &RowsMinus1, sizeof(RowsMinus1));
    if (RowsMinus1 == 0) {
        return 25;
    }

    return (UINT)RowsMinus1 + 1;
}

/************************************************************************/

static void RenderOutputBuffer(LPOUTPUT_CONTEXT Context, UINT ScrollOffset, UINT Rows) {
    UINT EndLine = ScrollOffset + Rows;

    if (EndLine > Context->LineCount) {
        EndLine = Context->LineCount;
    }

    for (UINT LineIndex = ScrollOffset; LineIndex < EndLine; LineIndex++) {
        UINT Start = Context->LineOffsets[LineIndex];
        UINT End = (LineIndex + 1 < Context->LineCount) ?
            Context->LineOffsets[LineIndex + 1] :
            Context->BufferLength;

        for (UINT Index = Start; Index < End; Index++) {
            OutputChar((U8)Context->Buffer[Index]);
        }
    }
}

/************************************************************************/

/**
 * @brief Draw page header and table headings.
 * @param Context Output context.
 * @param Title Page title.
 * @param PageIndex Page index.
 */
void DrawPageHeader(LPOUTPUT_CONTEXT Context, LPCSTR Title, U8 PageIndex) {
    WriteFormatRaw(Context, TEXT("Build %s\r\n"), TEXT(BOOT_PAYLOAD_BUILD_ID));
    WriteFormatRaw(Context, TEXT("Page %u/%u: %s\r\n"), (U32)(PageIndex + 1), (U32)PAGE_COUNT, Title);
    WriteString(Context, TEXT("-------------------------------------------------------------\r\n"));
}

/************************************************************************/

/**
 * @brief Draw footer with navigation hints.
 */
void DrawFooter(LPOUTPUT_CONTEXT Context) {
    WriteString(Context, TEXT("-------------------------------------------------------------\r\n"));
    WriteString(Context, TEXT("[<-] Previous page  |  [->] Next page\r\n"));
}

/************************************************************************/

static void DrawPagePicIoApic(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    U8 Mask1 = InPortByte(PIC1_DATA);
    U8 Mask2 = InPortByte(PIC2_DATA);
    U8 Irr1 = ReadPICRegister(PIC1_COMMAND, 0x0A);
    U8 Irr2 = ReadPICRegister(PIC2_COMMAND, 0x0A);
    U8 Isr1 = ReadPICRegister(PIC1_COMMAND, 0x0B);
    U8 Isr2 = ReadPICRegister(PIC2_COMMAND, 0x0B);
    U16 PitCounter = ReadPITCounter0();
    U8 PitStatus = ReadPITStatus0();
    U8 ImcrValue;

    OutPortByte(0x22, 0x70);
    ImcrValue = InPortByte(0x23);

    DrawPageHeader(Context, TEXT("PIC / PIT / IOAPIC"), PageIndex);

    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("PIC Mask1"), TEXT("%x\r\n"), Mask1);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("PIC Mask2"), TEXT("%x\r\n"), Mask2);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("PIC IRR1"), TEXT("%x\r\n"), Irr1);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("PIC IRR2"), TEXT("%x\r\n"), Irr2);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("PIC ISR1"), TEXT("%x\r\n"), Isr1);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("PIC ISR2"), TEXT("%x\r\n"), Isr2);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IMCR Value"), TEXT("%x\r\n"), ImcrValue);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("PIT Counter"), TEXT("%u\r\n"), PitCounter);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("PIT Status"), TEXT("%x\r\n"), PitStatus);

    {
        U32 Base = IOAPIC_BASE_DEFAULT;
        U32 IdReg;
        U32 VerReg;
        U32 RedirLow;
        U32 RedirHigh;

        EnableA20Fast();
        IdReg = ReadIOApicRegister(Base, IOAPIC_REG_ID);
        VerReg = ReadIOApicRegister(Base, IOAPIC_REG_VER);
        RedirLow = ReadIOApicRegister(Base, IOAPIC_REG_REDTBL_BASE + (2 * 2));
        RedirHigh = ReadIOApicRegister(Base, IOAPIC_REG_REDTBL_BASE + (2 * 2) + 1);
        DisableA20Fast();

        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IOAPIC Base"), TEXT("%p\r\n"), Base);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IOAPIC ID"), TEXT("%x\r\n"), IdReg);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IOAPIC VER"), TEXT("%x\r\n"), VerReg);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IOAPIC Redir[2].L"), TEXT("%x\r\n"), RedirLow);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IOAPIC Redir[2].H"), TEXT("%x\r\n"), RedirHigh);
    }

    DrawFooter(Context);
}

/************************************************************************/

static void DrawPage(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    switch (PageIndex) {
        case 0:
            DrawPageAcpiMadt(Context, PageIndex);
            break;
        case 1:
            DrawPagePicIoApic(Context, PageIndex);
            break;
        case 2:
            DrawPageLapic(Context, PageIndex);
            break;
        case 3:
            DrawPageInterruptRouting(Context, PageIndex);
            break;
        case 4:
            DrawPageAhci(Context, PageIndex);
            break;
        case 5:
            DrawPageEhci(Context, PageIndex);
            break;
        case 6:
            DrawPageXhci(Context, PageIndex);
            break;
        case 7:
            DrawPageIdt(Context, PageIndex);
            break;
        case 8:
        default:
            DrawPageGdt(Context, PageIndex);
            break;
    }
}

/************************************************************************/

/**
 * @brief Busy delay to avoid flooding output.
 */
/**
 * @brief Entry point for the boot payload.
 * @param BootDrive Boot drive number.
 * @param PartitionLba Partition LBA.
 */
void BootMain(U32 BootDrive, U32 PartitionLba) {
    UNUSED(BootDrive);
    UNUSED(PartitionLba);

    OUTPUT_CONTEXT Context;
    U8 CurrentPage = 0;
    UINT ScrollOffsets[PAGE_COUNT];

    for (UINT Index = 0; Index < PAGE_COUNT; Index++) {
        ScrollOffsets[Index] = 0;
    }

    while (TRUE) {
        UINT ScreenRows = GetScreenRows() - 1;
        UINT MaxScroll = 0;

        OutputBufferReset(&Context);
        DrawPage(&Context, CurrentPage);

        if (Context.LineCount > ScreenRows) {
            MaxScroll = Context.LineCount - ScreenRows;
        }
        if (ScrollOffsets[CurrentPage] > MaxScroll) {
            ScrollOffsets[CurrentPage] = MaxScroll;
        }

        BootClearScreen();
        RenderOutputBuffer(&Context, ScrollOffsets[CurrentPage], ScreenRows);

        {
            U16 Key = BootReadKeyExtended();
            U8 ScanCode = (U8)((Key >> 8) & 0xFF);

            if (ScanCode == 0x4D) {
                CurrentPage = (U8)((CurrentPage + 1) % PAGE_COUNT);
            } else if (ScanCode == 0x4B) {
                CurrentPage = (U8)((CurrentPage + PAGE_COUNT - 1) % PAGE_COUNT);
            } else if (ScanCode == 0x48) {
                if (ScrollOffsets[CurrentPage] > 0) {
                    ScrollOffsets[CurrentPage]--;
                }
            } else if (ScanCode == 0x50) {
                if (ScrollOffsets[CurrentPage] < MaxScroll) {
                    ScrollOffsets[CurrentPage]++;
                }
            }
        }
    }

    WriteString(&Context, TEXT("[InterruptDump] Halting.\r\n"));
    Hang();
}
