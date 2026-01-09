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

#define IOAPIC_BASE_DEFAULT 0xFEC00000u
#define IOAPIC_REGSEL 0x00
#define IOAPIC_IOWIN 0x10
#define IOAPIC_REG_ID 0x00
#define IOAPIC_REG_VER 0x01
#define IOAPIC_REG_REDTBL_BASE 0x10

#define PAGE_COUNT 5

#define ACPI_RSDP_SCAN_START 0x000E0000u
#define ACPI_RSDP_SCAN_END   0x00100000u

/************************************************************************/

static U8 ReadLinearU8(U32 Address);
static U16 ReadLinearU16(U32 Address);
static U32 ReadLinearU32(U32 Address);

/************************************************************************/

typedef struct tag_OUTPUT_CONTEXT {
    STR TemporaryString[128];
} OUTPUT_CONTEXT, *LPOUTPUT_CONTEXT;

typedef struct PACKED tag_DESCRIPTOR_TABLE_PTR {
    U16 Limit;
    U32 Base;
} DESCRIPTOR_TABLE_PTR, *LPDESCRIPTOR_TABLE_PTR;

typedef struct PACKED tag_IDT_ENTRY_32 {
    U16 OffsetLow;
    U16 Selector;
    U8 Zero;
    U8 TypeAttr;
    U16 OffsetHigh;
} IDT_ENTRY_32, *LPIDT_ENTRY_32;

typedef struct PACKED tag_GDT_ENTRY {
    U16 LimitLow;
    U16 BaseLow;
    U8 BaseMid;
    U8 Access;
    U8 Granularity;
    U8 BaseHigh;
} GDT_ENTRY, *LPGDT_ENTRY;

typedef struct PACKED tag_RSDP {
    U8 Signature[8];
    U8 Checksum;
    U8 OemId[6];
    U8 Revision;
    U32 RsdtAddress;
    U32 Length;
    U32 XsdtAddressLow;
    U32 XsdtAddressHigh;
    U8 ExtendedChecksum;
    U8 Reserved[3];
} RSDP, *LPRSDP;

typedef struct PACKED tag_ACPI_TABLE_HEADER {
    U8 Signature[4];
    U32 Length;
    U8 Revision;
    U8 Checksum;
    U8 OemId[6];
    U8 OemTableId[8];
    U32 OemRevision;
    U32 CreatorId;
    U32 CreatorRevision;
} ACPI_TABLE_HEADER, *LPACPI_TABLE_HEADER;

typedef struct PACKED tag_MADT_HEADER {
    ACPI_TABLE_HEADER Header;
    U32 LocalApicAddress;
    U32 Flags;
} MADT_HEADER, *LPMADT_HEADER;

typedef struct PACKED tag_MADT_ENTRY_HEADER {
    U8 Type;
    U8 Length;
} MADT_ENTRY_HEADER, *LPMADT_ENTRY_HEADER;

typedef struct PACKED tag_MADT_IOAPIC_ENTRY {
    U8 Type;
    U8 Length;
    U8 IoApicId;
    U8 Reserved;
    U32 IoApicAddress;
    U32 GlobalSystemInterruptBase;
} MADT_IOAPIC_ENTRY, *LPMADT_IOAPIC_ENTRY;

typedef struct PACKED tag_MADT_INTERRUPT_OVERRIDE {
    U8 Type;
    U8 Length;
    U8 Bus;
    U8 Source;
    U32 GlobalSystemInterrupt;
    U16 Flags;
} MADT_INTERRUPT_OVERRIDE, *LPMADT_INTERRUPT_OVERRIDE;

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
 * @brief Output a zero-terminated string.
 * @param String Text to output.
 */
static void WriteString(LPCSTR String) {
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
 * @brief Check whether a key is available in BIOS keyboard buffer.
 * @return TRUE if a key is available.
 */
static BOOL IsKeyAvailable(void) {
    return BootIsKeyAvailable() != 0;
}

/************************************************************************/

/**
 * @brief Read a key from BIOS keyboard buffer.
 * @return Combined scan/ascii key in AX.
 */
static U16 ReadKey(void) {
    return BootReadKeyExtended();
}

/************************************************************************/

static U16 ReadKeyBlocking(void) {
    return BootReadKeyBlocking();
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
 * @brief Read a 16-bit value from a linear address using unreal mode.
 * @param Address Linear address.
 * @return 16-bit value.
 */
static U16 ReadLinearU16(U32 Address) {
    return BootReadLinearU16(Address);
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
static U32 ReadIOApicRegister(U32 Base, U8 Register) {
    WriteLinearU32(Base + IOAPIC_REGSEL, Register);
    return ReadLinearU32(Base + IOAPIC_IOWIN);
}

/************************************************************************/

static void CopyFromLinear(U32 Address, void* Destination, U32 Size) {
    U8* Out = (U8*)Destination;
    for (U32 Index = 0; Index < Size; Index++) {
        Out[Index] = ReadLinearU8(Address + Index);
    }
}

/************************************************************************/

static BOOL CompareLinear(U32 Address, const U8* Signature, U32 Size) {
    for (U32 Index = 0; Index < Size; Index++) {
        if (ReadLinearU8(Address + Index) != Signature[Index]) {
            return FALSE;
        }
    }
    return TRUE;
}

/************************************************************************/

static U8 ComputeChecksum(U32 Address, U32 Length) {
    U8 Sum = 0;
    for (U32 Index = 0; Index < Length; Index++) {
        Sum = (U8)(Sum + ReadLinearU8(Address + Index));
    }
    return Sum;
}

/************************************************************************/

static BOOL FindRsdp(U32* RsdpAddressOut) {
    U16 EbdaSegment = ReadLinearU16(0x40E);
    U32 EbdaBase = ((U32)EbdaSegment) << 4;
    const U8 RsdpSig[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
    U8 Revision;
    U32 Length;

    for (U32 Address = EbdaBase; Address < EbdaBase + 1024; Address += 16) {
        if (!CompareLinear(Address, RsdpSig, 8)) {
            continue;
        }

        if (ComputeChecksum(Address, 20) != 0) {
            continue;
        }

        Revision = ReadLinearU8(Address + 15);
        if (Revision >= 2) {
            Length = ReadLinearU32(Address + 20);
            if (Length < 36 || Length > 4096) {
                continue;
            }
            if (ComputeChecksum(Address, Length) != 0) {
                continue;
            }
        }

        *RsdpAddressOut = Address;
        return TRUE;
    }

    for (U32 Address = ACPI_RSDP_SCAN_START; Address < ACPI_RSDP_SCAN_END; Address += 16) {
        if (!CompareLinear(Address, RsdpSig, 8)) {
            continue;
        }

        if (ComputeChecksum(Address, 20) != 0) {
            continue;
        }

        Revision = ReadLinearU8(Address + 15);
        if (Revision >= 2) {
            Length = ReadLinearU32(Address + 20);
            if (Length < 36 || Length > 4096) {
                continue;
            }
            if (ComputeChecksum(Address, Length) != 0) {
                continue;
            }
        }

        *RsdpAddressOut = Address;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static U32 FindMadtFromRsdt(U32 RsdtAddress) {
    ACPI_TABLE_HEADER Header;
    CopyFromLinear(RsdtAddress, &Header, sizeof(Header));

    if (Header.Length < sizeof(Header)) {
        return 0;
    }

    U32 EntryCount = (Header.Length - sizeof(Header)) / sizeof(U32);
    U32 EntriesBase = RsdtAddress + sizeof(Header);

    for (U32 Index = 0; Index < EntryCount; Index++) {
        U32 TableAddress = ReadLinearU32(EntriesBase + Index * sizeof(U32));
        ACPI_TABLE_HEADER EntryHeader;
        CopyFromLinear(TableAddress, &EntryHeader, sizeof(EntryHeader));
        if (EntryHeader.Signature[0] == 'A' &&
            EntryHeader.Signature[1] == 'P' &&
            EntryHeader.Signature[2] == 'I' &&
            EntryHeader.Signature[3] == 'C') {
            return TableAddress;
        }
    }

    return 0;
}

/************************************************************************/

static U32 FindMadtFromXsdt(U32 XsdtAddress) {
    ACPI_TABLE_HEADER Header;
    CopyFromLinear(XsdtAddress, &Header, sizeof(Header));

    if (Header.Length < sizeof(Header)) {
        return 0;
    }

    U32 EntryCount = (Header.Length - sizeof(Header)) / 8;
    U32 EntriesBase = XsdtAddress + sizeof(Header);

    for (U32 Index = 0; Index < EntryCount; Index++) {
        U32 EntryLow = ReadLinearU32(EntriesBase + Index * 8);
        U32 EntryHigh = ReadLinearU32(EntriesBase + Index * 8 + 4);
        if (EntryHigh != 0) {
            continue;
        }
        ACPI_TABLE_HEADER EntryHeader;
        CopyFromLinear(EntryLow, &EntryHeader, sizeof(EntryHeader));
        if (EntryHeader.Signature[0] == 'A' &&
            EntryHeader.Signature[1] == 'P' &&
            EntryHeader.Signature[2] == 'I' &&
            EntryHeader.Signature[3] == 'C') {
            return EntryLow;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Draw page header and table headings.
 * @param Context Output context.
 * @param Title Page title.
 * @param PageIndex Page index.
 */
static void DrawPageHeader(LPOUTPUT_CONTEXT Context, LPCSTR Title, U8 PageIndex) {
    WriteString(TEXT("Early Boot Dump\r\n"));
    WriteFormat(Context, TEXT("Build               %s\r\n"), TEXT(BOOT_PAYLOAD_BUILD_ID));
    WriteFormat(Context, TEXT("Page %u/%u: %s\r\n"), (U32)(PageIndex + 1), (U32)PAGE_COUNT, Title);
    WriteString(TEXT("------------------------------------------------\r\n"));
    WriteString(TEXT("Field                 Value\r\n"));
    WriteString(TEXT("--------------------  ---------------\r\n"));
}

/************************************************************************/

/**
 * @brief Draw footer with navigation hints.
 */
static void DrawFooter(void) {
    WriteString(TEXT("------------------------------------------------\r\n"));
    WriteString(TEXT("ESC: next page (loop)\r\n"));
    WriteString(TEXT("Auto refresh disabled.\r\n"));
}

/************************************************************************/

static void DrawPageAcpiMadt(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    U32 RsdpAddress = 0;
    RSDP Rsdp;
    U32 MadtAddress = 0;
    MADT_HEADER MadtHeader;
    U32 LocalApicCount = 0;
    U32 IoApicCount = 0;
    U32 OverrideCount = 0;
    U32 Offset = 0;
    U32 OverridePrinted = 0;

    DrawPageHeader(Context, TEXT("ACPI MADT"), PageIndex);

    if (!FindRsdp(&RsdpAddress)) {
        WriteString(TEXT("RSDP                 Not Found\r\n"));
        DrawFooter();
        return;
    }

    CopyFromLinear(RsdpAddress, &Rsdp, sizeof(Rsdp));
    WriteFormat(Context, TEXT("RSDP Address         0x%08x\r\n"), RsdpAddress);
    WriteFormat(Context, TEXT("RSDP Revision        %u\r\n"), (U32)Rsdp.Revision);
    WriteFormat(Context, TEXT("RSDP Checksum        0x%02x\r\n"), (U32)ComputeChecksum(RsdpAddress, (Rsdp.Revision >= 2) ? Rsdp.Length : 20));

    if (Rsdp.Revision >= 2 && Rsdp.XsdtAddressLow != 0) {
        MadtAddress = FindMadtFromXsdt(Rsdp.XsdtAddressLow);
        WriteFormat(Context, TEXT("XSDT Address         0x%08x\r\n"), Rsdp.XsdtAddressLow);
    } else {
        MadtAddress = FindMadtFromRsdt(Rsdp.RsdtAddress);
        WriteFormat(Context, TEXT("RSDT Address         0x%08x\r\n"), Rsdp.RsdtAddress);
    }

    if (MadtAddress == 0) {
        WriteString(TEXT("MADT Address         Not Found\r\n"));
        DrawFooter();
        return;
    }

    CopyFromLinear(MadtAddress, &MadtHeader, sizeof(MadtHeader));
    if (MadtHeader.Header.Length < sizeof(MADT_HEADER) || MadtHeader.Header.Length > 0x10000) {
        WriteString(TEXT("MADT Length          Invalid\r\n"));
        DrawFooter();
        return;
    }
    WriteFormat(Context, TEXT("MADT Address         0x%08x\r\n"), MadtAddress);
    WriteFormat(Context, TEXT("Local APIC Address   0x%08x\r\n"), MadtHeader.LocalApicAddress);
    WriteFormat(Context, TEXT("MADT Flags           0x%08x\r\n"), MadtHeader.Flags);

    Offset = sizeof(MADT_HEADER);
    while (Offset + sizeof(MADT_ENTRY_HEADER) <= MadtHeader.Header.Length) {
        MADT_ENTRY_HEADER EntryHeader;
        CopyFromLinear(MadtAddress + Offset, &EntryHeader, sizeof(EntryHeader));
        if (EntryHeader.Length < sizeof(MADT_ENTRY_HEADER)) {
            break;
        }

        if (EntryHeader.Type == 0) {
            LocalApicCount++;
        } else if (EntryHeader.Type == 1) {
            IoApicCount++;
        } else if (EntryHeader.Type == 2) {
            OverrideCount++;
            if (OverridePrinted < 3) {
                MADT_INTERRUPT_OVERRIDE Override;
                CopyFromLinear(MadtAddress + Offset, &Override, sizeof(Override));
                WriteFormat(Context, TEXT("Override %u         Bus=%u Src=%u GSI=%u\r\n"),
                    OverridePrinted,
                    (U32)Override.Bus,
                    (U32)Override.Source,
                    Override.GlobalSystemInterrupt);
                OverridePrinted++;
            }
        }

        Offset += EntryHeader.Length;
    }

    WriteFormat(Context, TEXT("Local APIC Count     %u\r\n"), LocalApicCount);
    WriteFormat(Context, TEXT("IO APIC Count        %u\r\n"), IoApicCount);
    WriteFormat(Context, TEXT("Override Count       %u\r\n"), OverrideCount);

    DrawFooter();
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
    U8 ImcrValue;

    OutPortByte(0x22, 0x70);
    ImcrValue = InPortByte(0x23);

    DrawPageHeader(Context, TEXT("PIC / IOAPIC"), PageIndex);

    WriteFormat(Context, TEXT("PIC Mask1           0x%02x\r\n"), Mask1);
    WriteFormat(Context, TEXT("PIC Mask2           0x%02x\r\n"), Mask2);
    WriteFormat(Context, TEXT("PIC IRR1            0x%02x\r\n"), Irr1);
    WriteFormat(Context, TEXT("PIC IRR2            0x%02x\r\n"), Irr2);
    WriteFormat(Context, TEXT("PIC ISR1            0x%02x\r\n"), Isr1);
    WriteFormat(Context, TEXT("PIC ISR2            0x%02x\r\n"), Isr2);
    WriteFormat(Context, TEXT("IMCR Value          0x%02x\r\n"), ImcrValue);
    WriteFormat(Context, TEXT("PIT Counter         %u\r\n"), PitCounter);

    {
        U32 Base = IOAPIC_BASE_DEFAULT;
        U32 IdReg = ReadIOApicRegister(Base, IOAPIC_REG_ID);
        U32 VerReg = ReadIOApicRegister(Base, IOAPIC_REG_VER);
        U32 RedirLow = ReadIOApicRegister(Base, IOAPIC_REG_REDTBL_BASE + (2 * 2));
        U32 RedirHigh = ReadIOApicRegister(Base, IOAPIC_REG_REDTBL_BASE + (2 * 2) + 1);

        WriteFormat(Context, TEXT("IOAPIC Base         0x%08x\r\n"), Base);
        WriteFormat(Context, TEXT("IOAPIC ID           0x%08x\r\n"), IdReg);
        WriteFormat(Context, TEXT("IOAPIC VER          0x%08x\r\n"), VerReg);
        WriteFormat(Context, TEXT("IOAPIC Redir[2].L   0x%08x\r\n"), RedirLow);
        WriteFormat(Context, TEXT("IOAPIC Redir[2].H   0x%08x\r\n"), RedirHigh);
    }

    DrawFooter();
}

/************************************************************************/

static void DrawPagePit(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    U16 Counter = ReadPITCounter0();
    U8 Status = ReadPITStatus0();

    DrawPageHeader(Context, TEXT("PIT"), PageIndex);
    WriteFormat(Context, TEXT("Counter             %u\r\n"), Counter);
    WriteFormat(Context, TEXT("Status              0x%02x\r\n"), Status);
    DrawFooter();
}

/************************************************************************/

static void DrawPageIdt(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    DESCRIPTOR_TABLE_PTR Idtr;
    IDT_ENTRY_32 Entry;

    BootStoreIdt((U32)&Idtr);

    DrawPageHeader(Context, TEXT("IDT"), PageIndex);
    WriteFormat(Context, TEXT("IDT Base            0x%08x\r\n"), Idtr.Base);
    WriteFormat(Context, TEXT("IDT Limit           0x%04x\r\n"), Idtr.Limit);

    for (U32 Vector = 0x20; Vector < 0x24; Vector++) {
        CopyFromLinear(Idtr.Base + Vector * sizeof(IDT_ENTRY_32), &Entry, sizeof(Entry));
        U32 Offset = ((U32)Entry.OffsetHigh << 16) | Entry.OffsetLow;
        WriteFormat(Context, TEXT("Vec 0x%02x           Off=0x%08x Sel=0x%04x\r\n"),
            Vector, Offset, (U32)Entry.Selector);
    }

    DrawFooter();
}

/************************************************************************/

static void DrawPageGdt(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
    DESCRIPTOR_TABLE_PTR Gdtr;
    GDT_ENTRY Entry;

    BootStoreGdt((U32)&Gdtr);

    DrawPageHeader(Context, TEXT("GDT"), PageIndex);
    WriteFormat(Context, TEXT("GDT Base            0x%08x\r\n"), Gdtr.Base);
    WriteFormat(Context, TEXT("GDT Limit           0x%04x\r\n"), Gdtr.Limit);

    for (U32 Index = 0; Index < 4; Index++) {
        CopyFromLinear(Gdtr.Base + Index * sizeof(GDT_ENTRY), &Entry, sizeof(Entry));
        U32 Base = (U32)Entry.BaseLow |
            ((U32)Entry.BaseMid << 16) |
            ((U32)Entry.BaseHigh << 24);
        U32 Limit = (U32)Entry.LimitLow | (((U32)Entry.Granularity & 0x0F) << 16);
        WriteFormat(Context, TEXT("Idx %u             Base=0x%08x Lim=0x%05x\r\n"),
            Index, Base, Limit);
    }

    DrawFooter();
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
            DrawPagePit(Context, PageIndex);
            break;
        case 3:
            DrawPageIdt(Context, PageIndex);
            break;
        case 4:
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
    EnableA20();
    BootEnableInterrupts();

    while (TRUE) {
        DrawPage(&Context, CurrentPage);

        {
            U16 Key = ReadKeyBlocking();
            U8 Character = (U8)(Key & 0xFF);
            U8 ScanCode = (U8)((Key >> 8) & 0xFF);

            if (Character == 27 || ScanCode == 0x01) {
                CurrentPage = (U8)((CurrentPage + 1) % PAGE_COUNT);
            }
        }

        while (IsKeyAvailable()) {
            (void)ReadKey();
        }
    }

    WriteString(TEXT("[InterruptDump] Halting.\r\n"));
    Hang();
}
