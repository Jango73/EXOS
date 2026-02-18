
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


    ACPI MADT page for the interrupt dump payload

\************************************************************************/

#include "../../kernel/include/CoreString.h"
#include "dumper.h"

/************************************************************************/
// Macros

#define ACPI_RSDP_SCAN_START 0x000E0000u
#define ACPI_RSDP_SCAN_END   0x00100000u

/************************************************************************/
// Type definitions

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
 * @brief Read a byte from a linear address using unreal mode.
 * @param Address Linear address.
 * @return Byte value.
 */
static U8 ReadLinearU8Value(U32 Address) {
    U8 Value = 0;

    CopyFromLinear(Address, &Value, sizeof(Value));

    return Value;
}

/************************************************************************/

/**
 * @brief Read a 16-bit value from a linear address using unreal mode.
 * @param Address Linear address.
 * @return 16-bit value.
 */
static U16 ReadLinearU16Value(U32 Address) {
    U16 Value = 0;

    CopyFromLinear(Address, &Value, sizeof(Value));

    return Value;
}

/************************************************************************/

/**
 * @brief Read a 32-bit value from a linear address using unreal mode.
 * @param Address Linear address.
 * @return 32-bit value.
 */
static U32 ReadLinearU32Value(U32 Address) {
    U32 Value = 0;

    CopyFromLinear(Address, &Value, sizeof(Value));

    return Value;
}

/************************************************************************/

/**
 * @brief Compare memory at a linear address to a signature.
 * @param Address Linear address.
 * @param Signature Signature bytes.
 * @param Size Signature size in bytes.
 * @return TRUE when the signature matches.
 */
static BOOL CompareLinear(U32 Address, const U8* Signature, U32 Size) {
    for (U32 Index = 0; Index < Size; Index++) {
        if (ReadLinearU8Value(Address + Index) != Signature[Index]) {
            return FALSE;
        }
    }
    return TRUE;
}

/************************************************************************/

/**
 * @brief Compute checksum for a linear memory region.
 * @param Address Linear address.
 * @param Length Length in bytes.
 * @return Checksum value.
 */
static U8 ComputeChecksum(U32 Address, U32 Length) {
    U8 Sum = 0;
    for (U32 Index = 0; Index < Length; Index++) {
        Sum = (U8)(Sum + ReadLinearU8Value(Address + Index));
    }
    return Sum;
}

/************************************************************************/

/**
 * @brief Locate the RSDP structure.
 * @param RsdpAddressOut Output RSDP address.
 * @return TRUE when an RSDP is found.
 */
static BOOL FindRsdp(U32* RsdpAddressOut) {
    U16 EbdaSegment = ReadLinearU16Value(0x40E);
    U32 EbdaBase = ((U32)EbdaSegment) << 4;
    const U8 RsdpSignature[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
    U8 Revision;
    U32 Length;

    for (U32 Address = EbdaBase; Address < EbdaBase + 1024; Address += 16) {
        if (!CompareLinear(Address, RsdpSignature, 8)) {
            continue;
        }

        if (ComputeChecksum(Address, 20) != 0) {
            continue;
        }

        Revision = ReadLinearU8Value(Address + 15);
        if (Revision >= 2) {
            Length = ReadLinearU32Value(Address + 20);
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
        if (!CompareLinear(Address, RsdpSignature, 8)) {
            continue;
        }

        if (ComputeChecksum(Address, 20) != 0) {
            continue;
        }

        Revision = ReadLinearU8Value(Address + 15);
        if (Revision >= 2) {
            Length = ReadLinearU32Value(Address + 20);
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

/**
 * @brief Find the MADT table address from the RSDT.
 * @param RsdtAddress RSDT address.
 * @return MADT address or 0 when missing.
 */
static U32 FindMadtFromRsdt(U32 RsdtAddress) {
    ACPI_TABLE_HEADER Header;
    CopyFromLinear(RsdtAddress, &Header, sizeof(Header));

    if (Header.Length < sizeof(Header)) {
        return 0;
    }

    U32 EntryCount = (Header.Length - sizeof(Header)) / sizeof(U32);
    U32 EntriesBase = RsdtAddress + sizeof(Header);

    for (U32 Index = 0; Index < EntryCount; Index++) {
        U32 TableAddress = ReadLinearU32Value(EntriesBase + Index * sizeof(U32));
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

/**
 * @brief Find the MADT table address from the XSDT.
 * @param XsdtAddress XSDT address.
 * @return MADT address or 0 when missing.
 */
static U32 FindMadtFromXsdt(U32 XsdtAddress) {
    ACPI_TABLE_HEADER Header;
    CopyFromLinear(XsdtAddress, &Header, sizeof(Header));

    if (Header.Length < sizeof(Header)) {
        return 0;
    }

    U32 EntryCount = (Header.Length - sizeof(Header)) / 8;
    U32 EntriesBase = XsdtAddress + sizeof(Header);

    for (U32 Index = 0; Index < EntryCount; Index++) {
        U32 EntryLow = ReadLinearU32Value(EntriesBase + Index * 8);
        U32 EntryHigh = ReadLinearU32Value(EntriesBase + Index * 8 + 4);
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
 * @brief Draw ACPI MADT information page.
 * @param Context Output context.
 * @param PageIndex Page index.
 */
void DrawPageAcpiMadt(LPOUTPUT_CONTEXT Context, U8 PageIndex) {
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
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("RSDP"), TEXT("Not Found\r\n"));
        DrawFooter(Context);
        return;
    }

    CopyFromLinear(RsdpAddress, &Rsdp, sizeof(Rsdp));
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("RSDP Address"), TEXT("%p\r\n"), RsdpAddress);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("RSDP Revision"), TEXT("%u\r\n"), (U32)Rsdp.Revision);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("RSDP Checksum"), TEXT("%x\r\n"),
        (U32)ComputeChecksum(RsdpAddress, (Rsdp.Revision >= 2) ? Rsdp.Length : 20));

    if (Rsdp.Revision >= 2 && Rsdp.XsdtAddressLow != 0) {
        MadtAddress = FindMadtFromXsdt(Rsdp.XsdtAddressLow);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("XSDT Address"), TEXT("%p\r\n"), Rsdp.XsdtAddressLow);
    } else {
        MadtAddress = FindMadtFromRsdt(Rsdp.RsdtAddress);
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("RSDT Address"), TEXT("%p\r\n"), Rsdp.RsdtAddress);
    }

    if (MadtAddress == 0) {
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("MADT Address"), TEXT("Not Found\r\n"));
        DrawFooter(Context);
        return;
    }

    CopyFromLinear(MadtAddress, &MadtHeader, sizeof(MadtHeader));
    if (MadtHeader.Header.Length < sizeof(MADT_HEADER) || MadtHeader.Header.Length > 0x10000) {
        WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("MADT Length"), TEXT("Invalid\r\n"));
        DrawFooter(Context);
        return;
    }
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("MADT Address"), TEXT("%p\r\n"), MadtAddress);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Local APIC Address"), TEXT("%p\r\n"), MadtHeader.LocalApicAddress);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("MADT Flags"), TEXT("%x\r\n"), MadtHeader.Flags);

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
                STR Label[32];
                StringPrintFormat(Label, TEXT("Override %u"), OverridePrinted);
                WriteFormat(Context, OUTPUT_VALUE_COLUMN, Label, TEXT("Bus=%u Src=%u GSI=%u\r\n"),
                    (U32)Override.Bus,
                    (U32)Override.Source,
                    Override.GlobalSystemInterrupt);
                OverridePrinted++;
            }
        }

        Offset += EntryHeader.Length;
    }

    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Local APIC Count"), TEXT("%u\r\n"), LocalApicCount);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("IO APIC Count"), TEXT("%u\r\n"), IoApicCount);
    WriteFormat(Context, OUTPUT_VALUE_COLUMN, TEXT("Override Count"), TEXT("%u\r\n"), OverrideCount);

    DrawFooter(Context);
}
