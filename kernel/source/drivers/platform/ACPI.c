
/************************************************************************\

    EXOS Kernel
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


    ACPI (Advanced Configuration and Power Interface)

\************************************************************************/

#include "drivers/platform/ACPI.h"

#include "Base.h"
#include "Console.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "CoreString.h"
#include "System.h"

/************************************************************************/

#define ACPI_VER_MAJOR 1
#define ACPI_VER_MINOR 0

static UINT ACPIDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION ACPIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = ACPI_VER_MAJOR,
    .VersionMinor = ACPI_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "ACPI",
    .Flags = 0,
    .Command = ACPIDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the ACPI driver descriptor.
 * @return Pointer to the ACPI driver.
 */
LPDRIVER ACPIGetDriver(void) {
    return &ACPIDriver;
}

/************************************************************************/
// Global ACPI configuration

static ACPI_CONFIG DATA_SECTION G_AcpiConfig = {0};
static LPACPI_RSDP DATA_SECTION G_RSDP = NULL;
static LPACPI_RSDT DATA_SECTION G_RSDT = NULL;
static LPACPI_XSDT DATA_SECTION G_XSDT = NULL;
static LPACPI_MADT DATA_SECTION G_MADT = NULL;
static LPACPI_FADT DATA_SECTION G_FADT = NULL;
static LPACPI_TABLE_HEADER DATA_SECTION G_DSDT = NULL;
static UINT DATA_SECTION G_RsdpLength = 0;
static UINT DATA_SECTION G_RsdtLength = 0;
static UINT DATA_SECTION G_XsdtLength = 0;
static UINT DATA_SECTION G_MadtLength = 0;
static UINT DATA_SECTION G_FadtLength = 0;
static UINT DATA_SECTION G_DsdtLength = 0;

static BOOL ParseS5SleepType(LPACPI_TABLE_HEADER Dsdt, UINT Length);
static BOOL EnsureFadtLoaded(void);

// Arrays to store discovered hardware
static IO_APIC_INFO DATA_SECTION G_IoApicInfo[8];                    // Support up to 8 I/O APICs
static LOCAL_APIC_INFO DATA_SECTION G_LocalApicInfo[32];             // Support up to 32 Local APICs
static INTERRUPT_OVERRIDE_INFO DATA_SECTION G_InterruptOverrides[24]; // Support up to 24 overrides

/************************************************************************/

/**
 * @brief Calculate checksum of a memory region.
 * @param Data Pointer to data.
 * @param Length Length of data in bytes.
 * @return Sum of all bytes (should be 0 for valid ACPI tables).
 */
static U8 CalculateChecksum(LPCVOID Data, U32 Length) {
    U8 Sum = 0;
    const U8* Bytes = (const U8*)Data;

    for (U32 i = 0; i < Length; i++) {
        Sum += Bytes[i];
    }

    return Sum;
}

/************************************************************************/

/**
 * @brief Decode AML package length.
 * @param Bytes Pointer to AML buffer positioned at package length byte.
 * @param Remaining Remaining bytes in buffer starting at Bytes.
 * @param LengthOut Decoded length.
 * @return TRUE if decoding succeeded.
 */
static BOOL DecodeAmlPackageLength(const U8* Bytes, UINT Remaining, U32* LengthOut) {
    if (Remaining == 0) {
        return FALSE;
    }

    U8 First = Bytes[0];
    U8 ByteCount = (U8)((First >> 6) & 0x03);
    U32 Length = First & 0x3F;

    if (ByteCount == 0) {
        *LengthOut = Length;
        return TRUE;
    }

    if (ByteCount >= 4 || Remaining <= ByteCount) {
        return FALSE;
    }

    for (U8 i = 0; i < ByteCount; i++) {
        Length |= ((U32)Bytes[1 + i]) << (8 * i + 4);
    }

    *LengthOut = Length;
    return TRUE;
}

/**
 * @brief Search for RSDP in a memory range.
 * @param StartPhysical Start of search range (physical address).
 * @param Length Length of search range.
 * @return Physical address of RSDP if found, 0 otherwise.
 */
static PHYSICAL SearchRSDPInRange(PHYSICAL StartPhysical, U32 Length) {
    PHYSICAL EndPhysical = StartPhysical + Length;

    for (PHYSICAL Address = StartPhysical; Address < EndPhysical; Address += 16) {
        ACPI_RSDP Candidate;

        if (!ReadPhysicalMemory(Address, &Candidate, sizeof(Candidate))) {
            continue;
        }

        if (MemoryCompare(Candidate.Signature, "RSD PTR ", 8) != 0) {
            continue;
        }

        if (CalculateChecksum(&Candidate, 20) != 0) {
            continue;
        }

        return Address;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Find and validate Root System Description Pointer (RSDP).
 * @return Pointer to RSDP if found and valid, NULL otherwise.
 */
LPACPI_RSDP FindRSDP(void) {

    PHYSICAL RsdpPhysical = KernelStartup.RsdpPhysical;
    BOOL FromBootloader = (RsdpPhysical != 0);

    if (FromBootloader != FALSE) {
    }

    if (RsdpPhysical == 0) {
        // Search in EBDA (Extended BIOS Data Area)
        // EBDA segment is stored at 0x40E (physical address 0x40E)
        U16 EbdaSegment = 0;
        if (ReadPhysicalMemory(0x40E, &EbdaSegment, sizeof(EbdaSegment))) {
            PHYSICAL EbdaAddress = ((PHYSICAL)EbdaSegment) << 4;  // Convert segment to physical address
            if (EbdaAddress != 0 && EbdaAddress < 0x100000) {
                RsdpPhysical = SearchRSDPInRange(EbdaAddress, 1024);  // Search first 1KB of EBDA
            }
        }

        // If not found in EBDA, search in standard BIOS ROM area (0xE0000 - 0xFFFFF)
        if (RsdpPhysical == 0) {
            RsdpPhysical = SearchRSDPInRange(0xE0000, 0x20000);  // 128KB range
        }
    }

    if (RsdpPhysical == 0) {
        return NULL;
    }

    ACPI_RSDP RsdpCopy;
    if (!ReadPhysicalMemory(RsdpPhysical, &RsdpCopy, sizeof(RsdpCopy))) {
        return NULL;
    }

    UINT RsdpLength = 20;
    if (RsdpCopy.Revision >= 2) {
        if (RsdpCopy.Length == 0 || RsdpCopy.Length > sizeof(ACPI_RSDP)) {
            return NULL;
        }

        U8 ExtendedChecksum = CalculateChecksum(&RsdpCopy, RsdpCopy.Length);
        if (ExtendedChecksum != 0) {
            return NULL;
        }

        RsdpLength = RsdpCopy.Length;
    } else {
        if (CalculateChecksum(&RsdpCopy, RsdpLength) != 0) {
            return NULL;
        }
    }

    LINEAR PermanentAddress = MapIOMemory(RsdpPhysical, RsdpLength);
    if (PermanentAddress == 0) {
        return NULL;
    }

    LPACPI_RSDP Rsdp = (LPACPI_RSDP)PermanentAddress;

    G_RsdpLength = RsdpLength;
    return Rsdp;
}

/************************************************************************/

/**
 * @brief Validate ACPI table checksum.
 * @param Table Pointer to ACPI table header.
 * @return TRUE if checksum is valid, FALSE otherwise.
 */
BOOL ValidateACPITableChecksum(LPACPI_TABLE_HEADER Table) {
    if (Table == NULL) return FALSE;

    U8 Checksum = CalculateChecksum(Table, Table->Length);
    BOOL Valid = (Checksum == 0);


    return Valid;
}

/************************************************************************/

/**
 * @brief Map an ACPI table entry if it matches the expected signature.
 * @param PhysicalAddress Physical address of the table entry.
 * @param Signature Expected 4-character signature.
 * @param PermanentMapping Output flag set to TRUE if a permanent IO mapping was created.
 * @param MappedLength Output length of the permanent mapping when created.
 * @return Pointer to the mapped table when successful, NULL otherwise.
 */
static LPACPI_TABLE_HEADER AcquireACPITable(PHYSICAL PhysicalAddress,
                                            LPCSTR Signature,
                                            BOOL* PermanentMapping,
                                            UINT* MappedLength) {
    if (PermanentMapping != NULL) *PermanentMapping = FALSE;
    if (MappedLength != NULL) *MappedLength = 0;

    ACPI_TABLE_HEADER Header;
    if (!ReadPhysicalMemory(PhysicalAddress, &Header, sizeof(Header))) {
        return NULL;
    }

    if (MemoryCompare(Header.Signature, Signature, 4) != 0) {
        return NULL;
    }

    if (Header.Length < sizeof(ACPI_TABLE_HEADER)) {
        return NULL;
    }

    LINEAR PermanentAddress = MapIOMemory(PhysicalAddress, Header.Length);
    if (PermanentAddress == 0) {
        return NULL;
    }

    if (PermanentMapping != NULL) {
        *PermanentMapping = TRUE;
    }

    if (MappedLength != NULL) {
        *MappedLength = Header.Length;
    }

    return (LPACPI_TABLE_HEADER)PermanentAddress;
}

/************************************************************************/

/**
 * @brief Find an ACPI table by signature.
 * @param Signature 4-character table signature (e.g., "APIC").
 * @return Pointer to table if found and valid, NULL otherwise.
 */
LPACPI_TABLE_HEADER FindACPITable(LPCSTR Signature) {

    if (G_RSDT == NULL && G_XSDT == NULL) {
        return NULL;
    }

    // Prefer XSDT if available (ACPI 2.0+)
    SAFE_USE(G_XSDT) {
        U32 EntryCount = (G_XSDT->Header.Length - sizeof(ACPI_TABLE_HEADER)) / sizeof(U64);

        for (U32 i = 0; i < EntryCount; i++) {
            U64 EntryAddress = G_XSDT->Entry[i];

#if defined(__EXOS_32__)
            U32 EntryHigh = U64_High32(EntryAddress);
            U32 EntryLow = U64_Low32(EntryAddress);
            if (EntryHigh != 0) {
                continue;
            }

            PHYSICAL PhysicalAddress = (PHYSICAL)EntryLow;
#else
            PHYSICAL PhysicalAddress = (PHYSICAL)EntryAddress;
#endif
            UINT MappedLength = 0;
            BOOL PermanentMapping = FALSE;

            LPACPI_TABLE_HEADER Table =
                AcquireACPITable(PhysicalAddress, Signature, &PermanentMapping, &MappedLength);
            if (Table == NULL) {
                continue;
            }

            if (ValidateACPITableChecksum(Table)) {
                return Table;
            }

            if (PermanentMapping) {
                UnMapIOMemory((LINEAR)Table, MappedLength);
            }
        }
    }

    // Search RSDT if XSDT is not available or didn't contain the table
    SAFE_USE(G_RSDT) {
        U32 EntryCount = (G_RSDT->Header.Length - sizeof(ACPI_TABLE_HEADER)) / sizeof(U32);

        for (U32 i = 0; i < EntryCount; i++) {
            PHYSICAL PhysicalAddress = (PHYSICAL)G_RSDT->Entry[i];
            UINT MappedLength = 0;
            BOOL PermanentMapping = FALSE;

            LPACPI_TABLE_HEADER Table =
                AcquireACPITable(PhysicalAddress, Signature, &PermanentMapping, &MappedLength);
            if (Table == NULL) {
                continue;
            }

            if (ValidateACPITableChecksum(Table)) {
                return Table;
            }

            if (PermanentMapping) {
                UnMapIOMemory((LINEAR)Table, MappedLength);
            }
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Parse Multiple APIC Description Table (MADT).
 * @return TRUE if MADT was found and parsed successfully, FALSE otherwise.
 */
BOOL ParseMADT(void) {

    // Find MADT table
    G_MADT = (LPACPI_MADT)FindACPITable(TEXT("APIC"));
    if (G_MADT == NULL) {
        return FALSE;
    }

    G_MadtLength = G_MADT->Header.Length;

    // Store Local APIC address
    G_AcpiConfig.LocalApicAddress = (PHYSICAL)G_MADT->LocalApicAddress;

    // Parse MADT entries
    U32 EntryOffset = 0;
    U32 TotalLength = G_MADT->Header.Length - sizeof(ACPI_MADT);

    while (EntryOffset < TotalLength) {
        LPACPI_MADT_ENTRY_HEADER Entry = (LPACPI_MADT_ENTRY_HEADER)&G_MADT->InterruptController[EntryOffset];

        if (Entry->Length == 0) {
            break;
        }

        switch (Entry->Type) {
            case ACPI_MADT_TYPE_LOCAL_APIC: {
                LPACPI_MADT_LOCAL_APIC LocalApic = (LPACPI_MADT_LOCAL_APIC)Entry;
                if (G_AcpiConfig.LocalApicCount < 32) {
                    G_LocalApicInfo[G_AcpiConfig.LocalApicCount].ProcessorId = LocalApic->ProcessorId;
                    G_LocalApicInfo[G_AcpiConfig.LocalApicCount].ApicId = LocalApic->ApicId;
                    G_LocalApicInfo[G_AcpiConfig.LocalApicCount].Flags = LocalApic->Flags;
                    G_AcpiConfig.LocalApicCount++;
                }
                break;
            }

            case ACPI_MADT_TYPE_IO_APIC: {
                LPACPI_MADT_IO_APIC IoApic = (LPACPI_MADT_IO_APIC)Entry;
                if (G_AcpiConfig.IoApicCount < 8) {
                    G_IoApicInfo[G_AcpiConfig.IoApicCount].IoApicId = IoApic->IoApicId;
                    G_IoApicInfo[G_AcpiConfig.IoApicCount].IoApicAddress = (PHYSICAL)IoApic->IoApicAddress;
                    G_IoApicInfo[G_AcpiConfig.IoApicCount].GlobalSystemInterruptBase = IoApic->GlobalSystemInterruptBase;
                    G_IoApicInfo[G_AcpiConfig.IoApicCount].MaxRedirectionEntry = 0; // Will be read from I/O APIC later
                    G_AcpiConfig.IoApicCount++;
                }
                break;
            }

            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                LPACPI_MADT_INTERRUPT_OVERRIDE Override = (LPACPI_MADT_INTERRUPT_OVERRIDE)Entry;
                if (G_AcpiConfig.InterruptOverrideCount < 24) {
                    G_InterruptOverrides[G_AcpiConfig.InterruptOverrideCount].Bus = Override->Bus;
                    G_InterruptOverrides[G_AcpiConfig.InterruptOverrideCount].Source = Override->Source;
                    G_InterruptOverrides[G_AcpiConfig.InterruptOverrideCount].GlobalSystemInterrupt = Override->GlobalSystemInterrupt;
                    G_InterruptOverrides[G_AcpiConfig.InterruptOverrideCount].Flags = Override->Flags;
                    G_AcpiConfig.InterruptOverrideCount++;
                }
                break;
            }

            case ACPI_MADT_TYPE_LOCAL_APIC_NMI: {
                break;
            }

            default:
                break;
        }

        EntryOffset += Entry->Length;
    }

    // Set configuration flags
    G_AcpiConfig.UseLocalApic = (G_AcpiConfig.LocalApicCount > 0);
    G_AcpiConfig.UseIoApic = (G_AcpiConfig.IoApicCount > 0);


    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize ACPI and parse tables.
 * @return TRUE if ACPI is available and initialized successfully, FALSE otherwise.
 */
BOOL InitializeACPI(void) {

    // Clear configuration
    MemorySet(&G_AcpiConfig, 0, sizeof(ACPI_CONFIG));
    G_RsdpLength = 0;
    G_RsdtLength = 0;
    G_XsdtLength = 0;
    G_MadtLength = 0;
    G_FadtLength = 0;

    // Find RSDP
    G_RSDP = FindRSDP();
    if (G_RSDP == NULL) {
        return FALSE;
    }

    // Map and validate RSDT
    if (G_RSDP->RsdtAddress != 0) {
        PHYSICAL RsdtPhysical = (PHYSICAL)G_RSDP->RsdtAddress;

        ACPI_TABLE_HEADER RsdtHeader;
        if (!ReadPhysicalMemory(RsdtPhysical, &RsdtHeader, sizeof(RsdtHeader))) {
            G_RSDT = NULL;
        } else if (RsdtHeader.Length < sizeof(ACPI_TABLE_HEADER)) {
            G_RSDT = NULL;
        } else {
            LINEAR PermanentAddress = MapIOMemory(RsdtPhysical, RsdtHeader.Length);
            if (PermanentAddress != 0) {
                G_RSDT = (LPACPI_RSDT)PermanentAddress;
                G_RsdtLength = RsdtHeader.Length;
            } else {
                G_RSDT = NULL;
            }
        }

        if (G_RSDT != NULL) {
            if (G_RSDT->Header.Length < sizeof(ACPI_TABLE_HEADER)) {
                UnMapIOMemory((LINEAR)G_RSDT, RsdtHeader.Length);
                G_RsdtLength = 0;
                G_RSDT = NULL;
            } else if (!ValidateACPITableChecksum(&G_RSDT->Header)) {
                UnMapIOMemory((LINEAR)G_RSDT, RsdtHeader.Length);
                G_RsdtLength = 0;
                G_RSDT = NULL;
            } else {
            }
        }
    }

    // Map and validate XSDT if available (ACPI 2.0+)
    if (G_RSDP->Revision >= 2) {
        BOOL HasXsdt = FALSE;
        PHYSICAL XsdtPhysical = 0;

#if defined(__EXOS_32__)
        if (U64_Low32(G_RSDP->XsdtAddress) != 0 || U64_High32(G_RSDP->XsdtAddress) != 0) {
            if (U64_High32(G_RSDP->XsdtAddress) != 0) {
            } else {
                HasXsdt = TRUE;
                XsdtPhysical = (PHYSICAL)U64_Low32(G_RSDP->XsdtAddress);
            }
        }
#else
        if (!U64_EQUAL(G_RSDP->XsdtAddress, U64_0)) {
            HasXsdt = TRUE;
            XsdtPhysical = (PHYSICAL)G_RSDP->XsdtAddress;
        }
#endif

        if (HasXsdt) {

            ACPI_TABLE_HEADER XsdtHeader;
            if (!ReadPhysicalMemory(XsdtPhysical, &XsdtHeader, sizeof(XsdtHeader))) {
                G_XSDT = NULL;
            } else if (XsdtHeader.Length < sizeof(ACPI_TABLE_HEADER)) {
                G_XSDT = NULL;
            } else {
                LINEAR PermanentAddress = MapIOMemory(XsdtPhysical, XsdtHeader.Length);
                if (PermanentAddress != 0) {
                    G_XSDT = (LPACPI_XSDT)PermanentAddress;
                    G_XsdtLength = XsdtHeader.Length;
                } else {
                    G_XSDT = NULL;
                }
            }

            if (G_XSDT != NULL) {
                if (G_XSDT->Header.Length < sizeof(ACPI_TABLE_HEADER)) {
                    UnMapIOMemory((LINEAR)G_XSDT, XsdtHeader.Length);
                    G_XsdtLength = 0;
                    G_XSDT = NULL;
                } else if (!ValidateACPITableChecksum(&G_XSDT->Header)) {
                    UnMapIOMemory((LINEAR)G_XSDT, XsdtHeader.Length);
                    G_XsdtLength = 0;
                    G_XSDT = NULL;
                } else {
                }
            }
        }
    }

    // Check if we have at least one valid table
    if (G_RSDT == NULL && G_XSDT == NULL) {
        return FALSE;
    }

    // Parse MADT for APIC information
    if (!ParseMADT()) {
        return FALSE;
    }

    // Map FADT and parse _S5 sleep type if available
    if (EnsureFadtLoaded()) {
        if (G_FADT->Dsdt != 0) {
            UINT DsdtMappedLength = 0;
            BOOL PermanentMapping = FALSE;
            LPACPI_TABLE_HEADER Dsdt =
                AcquireACPITable((PHYSICAL)G_FADT->Dsdt, TEXT("DSDT"), &PermanentMapping, &DsdtMappedLength);
            if (Dsdt != NULL) {
                G_DSDT = Dsdt;
                G_DsdtLength = DsdtMappedLength;
                ParseS5SleepType(Dsdt, DsdtMappedLength);
            } else {
            }
        } else {
        }
    }

    G_AcpiConfig.Valid = TRUE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Get ACPI configuration.
 * @return Pointer to ACPI configuration structure.
 */
LPACPI_CONFIG GetACPIConfig(void) {
    return &G_AcpiConfig;
}

/************************************************************************/

/**
 * @brief Get I/O APIC information by index.
 * @param Index Index of I/O APIC (0-based).
 * @return Pointer to I/O APIC information, NULL if index is invalid.
 */
LPIO_APIC_INFO GetIOApicInfo(U32 Index) {
    if (Index >= G_AcpiConfig.IoApicCount) return NULL;
    return &G_IoApicInfo[Index];
}

/************************************************************************/

/**
 * @brief Get Local APIC information by index.
 * @param Index Index of Local APIC (0-based).
 * @return Pointer to Local APIC information, NULL if index is invalid.
 */
LPLOCAL_APIC_INFO GetLocalApicInfo(U32 Index) {
    if (Index >= G_AcpiConfig.LocalApicCount) return NULL;
    return &G_LocalApicInfo[Index];
}

/************************************************************************/

/**
 * @brief Get interrupt override information by index.
 * @param Index Index of interrupt override (0-based).
 * @return Pointer to interrupt override information, NULL if index is invalid.
 */
LPINTERRUPT_OVERRIDE_INFO GetInterruptOverrideInfo(U32 Index) {
    if (Index >= G_AcpiConfig.InterruptOverrideCount) return NULL;
    return &G_InterruptOverrides[Index];
}

/************************************************************************/

/**
 * @brief Parse the _S5 object in the DSDT to discover S5 sleep types.
 * @param Dsdt Pointer to mapped DSDT.
 * @param Length Length of mapped DSDT.
 * @return TRUE if valid S5 values were found.
 */
static BOOL ParseS5SleepType(LPACPI_TABLE_HEADER Dsdt, UINT Length) {
    const U8* Bytes = (const U8*)Dsdt;
    const U8* End = Bytes + Length;

    if (Length < 4) {
        return FALSE;
    }

    for (UINT i = 0; i + 4 < Length; i++) {
        // Look for NameOp ('_S5_') pattern: 0x08 '_' 'S' '5' '_'
        if (Bytes[i] != 0x08) {
            continue;
        }

        if (!(Bytes[i + 1] == '_' && Bytes[i + 2] == 'S' && Bytes[i + 3] == '5' && Bytes[i + 4] == '_')) {
            continue;
        }

        const U8* Cursor = Bytes + i + 5;
        if (Cursor >= End) {
            break;
        }

        // Expect PackageOp (0x12)
        if (*Cursor != 0x12) {
            continue;
        }
        Cursor++;

        U32 PackageLength = 0;
        if (!DecodeAmlPackageLength(Cursor, (UINT)(End - Cursor), &PackageLength)) {
            continue;
        }

        U8 PackagePrefixBytes = 1 + (U8)((Cursor[0] >> 6) & 0x03);
        Cursor += PackagePrefixBytes;
        if (Cursor >= End) {
            break;
        }

        // Next byte is element count, should be at least 2
        U8 ElementCount = *Cursor++;
        if (ElementCount < 2) {
            continue;
        }

        // Decode first two package elements: each can be BytePrefix (0x0A) or WordPrefix (0x0B/0x0C)
        for (UINT element = 0; element < 2; element++) {
            if (Cursor >= End) {
                return FALSE;
            }

            U8 Value = 0;
            if (*Cursor == 0x0A) { // ByteConst
                if (Cursor + 1 >= End) {
                    return FALSE;
                }
                Value = Cursor[1];
                Cursor += 2;
            } else if (*Cursor == 0x0B) { // WordConst
                if (Cursor + 2 >= End) {
                    return FALSE;
                }
                Value = Cursor[1];
                Cursor += 3;
            } else {
                // Unexpected AML encoding
                Value = *Cursor;
                Cursor++;
            }

            if (element == 0) {
                G_AcpiConfig.SlpTypS5A = Value;
            } else {
                G_AcpiConfig.SlpTypS5B = Value;
            }
        }

        G_AcpiConfig.S5Available = TRUE;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Ensure the FADT table is mapped.
 * @return TRUE if the FADT is available and mapped.
 */
static BOOL EnsureFadtLoaded(void) {
    if (G_FADT != NULL) {
        return TRUE;
    }

    G_FADT = (LPACPI_FADT)FindACPITable(TEXT("FACP"));
    if (G_FADT != NULL) {
        G_FadtLength = G_FADT->Header.Length;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Map an interrupt using override table.
 * @param IRQ Original IRQ number.
 * @return Global System Interrupt number, or IRQ if no override exists.
 */
U32 MapInterrupt(U8 IRQ) {
    for (U32 i = 0; i < G_AcpiConfig.InterruptOverrideCount; i++) {
        if (G_InterruptOverrides[i].Bus == 0 && G_InterruptOverrides[i].Source == IRQ) {
            return G_InterruptOverrides[i].GlobalSystemInterrupt;
        }
    }

    // No override found, return original IRQ
    return IRQ;
}

/************************************************************************/

/**
 * @brief Release ACPI resources without powering off.
 */
void ACPIShutdown(void) {

    if (G_FADT != NULL && G_FadtLength != 0) {
        UnMapIOMemory((LINEAR)G_FADT, G_FadtLength);
    }
    G_FADT = NULL;
    G_FadtLength = 0;

    if (G_MADT != NULL && G_MadtLength != 0) {
        UnMapIOMemory((LINEAR)G_MADT, G_MadtLength);
    }
    G_MADT = NULL;
    G_MadtLength = 0;

    if (G_DSDT != NULL && G_DsdtLength != 0) {
        UnMapIOMemory((LINEAR)G_DSDT, G_DsdtLength);
    }
    G_DSDT = NULL;
    G_DsdtLength = 0;

    if (G_XSDT != NULL && G_XsdtLength != 0) {
        UnMapIOMemory((LINEAR)G_XSDT, G_XsdtLength);
    }
    G_XSDT = NULL;
    G_XsdtLength = 0;

    if (G_RSDT != NULL && G_RsdtLength != 0) {
        UnMapIOMemory((LINEAR)G_RSDT, G_RsdtLength);
    }
    G_RSDT = NULL;
    G_RsdtLength = 0;

    if (G_RSDP != NULL && G_RsdpLength != 0) {
        UnMapIOMemory((LINEAR)G_RSDP, G_RsdpLength);
    }
    G_RSDP = NULL;
    G_RsdpLength = 0;

    MemorySet(&G_AcpiConfig, 0, sizeof(ACPI_CONFIG));
}

/************************************************************************/

/**
 * @brief Shutdown the system using ACPI.
 * This function attempts to put the system into ACPI sleep state S5 (power off).
 */
void ACPIPowerOff(void) {

    // Check if ACPI is available
    if (!G_AcpiConfig.Valid) {
        if (!InitializeACPI()) {
            return;
        }
    }

    // Ensure FADT table is available
    if (!EnsureFadtLoaded()) {
        return;
    }

    // Check if PM1 control block is available
    if (G_FADT->Pm1aControlBlock == 0) {
        return;
    }


    U8 SlpTypA = 7;
    U8 SlpTypB = 7;
    if (G_AcpiConfig.S5Available) {
        SlpTypA = G_AcpiConfig.SlpTypS5A;
        SlpTypB = G_AcpiConfig.SlpTypS5B;
    } else {
    }

    // For S5 sleep state, we set SLP_TYP to the parsed value and SLP_EN to 1
    U16 Pm1ControlValue = ((U16)SlpTypA << 10) | (1 << 13);


    // Write to PM1a control register
    OutPortWord(G_FADT->Pm1aControlBlock, Pm1ControlValue);

    // If PM1b control block is also available, write to it as well
    if (G_FADT->Pm1bControlBlock != 0) {
        OutPortWord(G_FADT->Pm1bControlBlock, ((U16)SlpTypB << 10) | (1 << 13));
    }

    // If we reach here, ACPI shutdown failed

    // Try alternative shutdown methods as fallback

    // Try QEMU/Bochs specific shutdown
    OutPortWord(0x604, 0x2000);  // QEMU shutdown
    OutPortWord(0xB004, 0x2000); // Bochs shutdown

}

/************************************************************************/

/**
 * @brief Reboot the system using ACPI.
 * This function attempts to perform a warm reboot through the ACPI reset register.
 */
void ACPIReboot(void) {

    if (!G_AcpiConfig.Valid) {
    } else {
        if (G_FADT == NULL) {
            G_FADT = (LPACPI_FADT)FindACPITable(TEXT("FACP"));
            if (G_FADT == NULL) {
            } else {
                G_FadtLength = G_FADT->Header.Length;
            }
        }

        if (G_FADT != NULL) {
            if (G_FADT->Header.Length >= sizeof(ACPI_FADT)
                && (G_FADT->ResetReg.AddressLow != 0 || G_FADT->ResetReg.AddressHigh != 0)) {
                if (G_FADT->ResetReg.AddressSpaceId == ACPI_ADDRESS_SPACE_SYSTEM_IO) {
                    if ((G_FADT->ResetReg.AccessSize == 0 || G_FADT->ResetReg.AccessSize == 1)
                        && G_FADT->ResetReg.RegisterBitWidth == 8
                        && G_FADT->ResetReg.RegisterBitOffset == 0) {
                        if (G_FADT->ResetReg.AddressHigh == 0) {
                            U16 ResetPort = (U16)G_FADT->ResetReg.AddressLow;
                            OutPortByte(ResetPort, G_FADT->ResetValue);
                            (void)InPortByte(0x80);
                            (void)InPortByte(0x80);
                        }
                    }
                }
            }
        }
    }



    OutPortByte(0xCF9, 0x02);
    (void)InPortByte(0x80);
    OutPortByte(0xCF9, 0x06);
    (void)InPortByte(0x80);


    Reboot();
    return;
}

/************************************************************************/

/**
 * @brief Driver command handler for ACPI initialization.
 *
 * DF_LOAD initializes ACPI (idempotent). DF_UNLOAD releases ACPI resources if
 * the driver was ready.
 */
static UINT ACPIDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((ACPIDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (InitializeACPI()) {
                ACPIDriver.Flags |= DRIVER_FLAG_READY;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_UNEXPECTED;

        case DF_UNLOAD:
            if ((ACPIDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            ACPIDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(ACPI_VER_MAJOR, ACPI_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
