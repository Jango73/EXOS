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

#include "../include/ACPI.h"

#include "../include/Base.h"
#include "../include/Console.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/String.h"
#include "../include/System.h"

/************************************************************************/
// Global ACPI configuration

static ACPI_CONFIG G_AcpiConfig = {0};
static LPACPI_RSDP G_RSDP = NULL;
static LPACPI_RSDT G_RSDT = NULL;
static LPACPI_XSDT G_XSDT = NULL;
static LPACPI_MADT G_MADT = NULL;
static LPACPI_FADT G_FADT = NULL;

// Arrays to store discovered hardware
static IO_APIC_INFO G_IoApicInfo[8];                    // Support up to 8 I/O APICs
static LOCAL_APIC_INFO G_LocalApicInfo[32];             // Support up to 32 Local APICs
static INTERRUPT_OVERRIDE_INFO G_InterruptOverrides[24]; // Support up to 24 overrides

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
 * @brief Search for RSDP in a memory range.
 * @param StartAddress Start of search range.
 * @param Length Length of search range.
 * @return Pointer to RSDP if found, NULL otherwise.
 */
static LPACPI_RSDP SearchRSDPInRange(LINEAR StartAddress, U32 Length) {
    DEBUG(TEXT("[SearchRSDPInRange] Searching range 0x%08X - 0x%08X"), StartAddress, StartAddress + Length);

    for (LINEAR Address = StartAddress; Address < StartAddress + Length; Address += 16) {
        if (!IsValidMemory(Address)) continue;

        LPACPI_RSDP Rsdp = (LPACPI_RSDP)Address;

        // Check signature "RSD PTR "
        if (MemoryCompare(Rsdp->Signature, "RSD PTR ", 8) == 0) {
            // Validate checksum for ACPI 1.0 portion
            U8 Checksum = CalculateChecksum(Rsdp, 20);
            if (Checksum == 0) {
                DEBUG(TEXT("[SearchRSDPInRange] Found valid RSDP at 0x%08X"), Address);
                return Rsdp;
            }
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Find and validate Root System Description Pointer (RSDP).
 * @return Pointer to RSDP if found and valid, NULL otherwise.
 */
LPACPI_RSDP FindRSDP(void) {
    DEBUG(TEXT("[FindRSDP] Enter"));

    LPACPI_RSDP Rsdp = NULL;

    // Search in EBDA (Extended BIOS Data Area)
    // EBDA segment is stored at 0x40E (physical address 0x40E)
    U16* EbdaSegment = (U16*)0x40E;
    if (IsValidMemory((LINEAR)EbdaSegment)) {
        LINEAR EbdaAddress = (*EbdaSegment) << 4;  // Convert segment to linear address
        if (EbdaAddress != 0 && EbdaAddress < 0x100000) {
            DEBUG(TEXT("[FindRSDP] Searching EBDA at 0x%08X"), EbdaAddress);
            Rsdp = SearchRSDPInRange(EbdaAddress, 1024);  // Search first 1KB of EBDA
        }
    }

    // If not found in EBDA, search in standard BIOS ROM area (0xE0000 - 0xFFFFF)
    if (Rsdp == NULL) {
        DEBUG(TEXT("[FindRSDP] Searching BIOS ROM area"));
        Rsdp = SearchRSDPInRange(0xE0000, 0x20000);  // 128KB range
    }

    SAFE_USE(Rsdp) {
        DEBUG(TEXT("[FindRSDP] RSDP found at 0x%08X, revision %d"), (U32)Rsdp, Rsdp->Revision);

        // For ACPI 2.0+, validate extended checksum
        if (Rsdp->Revision >= 2) {
            U8 ExtendedChecksum = CalculateChecksum(Rsdp, Rsdp->Length);
            if (ExtendedChecksum != 0) {
                DEBUG(TEXT("[FindRSDP] Extended checksum validation failed"));
                return NULL;
            }
        }
    } else {
        DEBUG(TEXT("[FindRSDP] RSDP not found"));
    }

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

    DEBUG(TEXT("[ValidateACPITableChecksum] Table %.4s, length %d, checksum %s"),
          Table->Signature, Table->Length, Valid ? "valid" : "invalid");

    return Valid;
}

/************************************************************************/

/**
 * @brief Find an ACPI table by signature.
 * @param Signature 4-character table signature (e.g., "APIC").
 * @return Pointer to table if found and valid, NULL otherwise.
 */
LPACPI_TABLE_HEADER FindACPITable(LPCSTR Signature) {
    DEBUG(TEXT("[FindACPITable] Looking for table %.4s"), Signature);

    if (G_RSDT == NULL && G_XSDT == NULL) {
        DEBUG(TEXT("[FindACPITable] No RSDT or XSDT available"));
        return NULL;
    }

    // Prefer XSDT if available (ACPI 2.0+)
    SAFE_USE(G_XSDT) {
        U32 EntryCount = (G_XSDT->Header.Length - sizeof(ACPI_TABLE_HEADER)) / sizeof(U64);
        DEBUG(TEXT("[FindACPITable] Searching XSDT with %d entries"), EntryCount);

        for (U32 i = 0; i < EntryCount; i++) {
            // For 32-bit systems, we only use the lower 32 bits of the 64-bit pointer
            if (G_XSDT->Entry[i].HI != 0) {
                DEBUG(TEXT("[FindACPITable] Skipping 64-bit address 0x%08X%08X"),
                      G_XSDT->Entry[i].HI, G_XSDT->Entry[i].LO);
                continue;
            }

            U32 PhysicalAddress = G_XSDT->Entry[i].LO;
            LINEAR TableAddress;

            // Map the physical address to virtual address
            if (PhysicalAddress < 0x100000) {
                // Table is in low memory, use direct mapping
                TableAddress = PhysicalAddress;
            } else {
                // Table is in high memory, needs to be mapped
                LINEAR MappedAddress = MapTempPhysicalPage(PhysicalAddress & ~0xFFF);
                if (MappedAddress == 0) {
                    DEBUG(TEXT("[FindACPITable] Failed to map table at 0x%08X"), PhysicalAddress);
                    continue;
                }
                TableAddress = MappedAddress + (PhysicalAddress & 0xFFF);
            }

            if (!IsValidMemory(TableAddress)) {
                DEBUG(TEXT("[FindACPITable] Table at 0x%08X not accessible"), TableAddress);
                continue;
            }

            LPACPI_TABLE_HEADER Table = (LPACPI_TABLE_HEADER)TableAddress;
            if (MemoryCompare(Table->Signature, Signature, 4) == 0) {
                if (ValidateACPITableChecksum(Table)) {
                    DEBUG(TEXT("[FindACPITable] Found table %.4s at physical 0x%08X, virtual 0x%08X"),
                          Signature, PhysicalAddress, TableAddress);
                    return Table;
                }
            }
        }
    }

    // Search RSDT if XSDT is not available or didn't contain the table
    SAFE_USE(G_RSDT) {
        U32 EntryCount = (G_RSDT->Header.Length - sizeof(ACPI_TABLE_HEADER)) / sizeof(U32);
        DEBUG(TEXT("[FindACPITable] Searching RSDT with %d entries"), EntryCount);

        for (U32 i = 0; i < EntryCount; i++) {
            U32 PhysicalAddress = G_RSDT->Entry[i];
            LINEAR TableAddress;

            // Map the physical address to virtual address
            if (PhysicalAddress < 0x100000) {
                // Table is in low memory, use direct mapping
                TableAddress = PhysicalAddress;
            } else {
                // Table is in high memory, needs to be mapped
                LINEAR MappedAddress = MapTempPhysicalPage(PhysicalAddress & ~0xFFF);
                if (MappedAddress == 0) {
                    DEBUG(TEXT("[FindACPITable] Failed to map table at 0x%08X"), PhysicalAddress);
                    continue;
                }
                TableAddress = MappedAddress + (PhysicalAddress & 0xFFF);
            }

            if (!IsValidMemory(TableAddress)) {
                DEBUG(TEXT("[FindACPITable] Table at 0x%08X not accessible"), TableAddress);
                continue;
            }

            LPACPI_TABLE_HEADER Table = (LPACPI_TABLE_HEADER)TableAddress;
            if (MemoryCompare(Table->Signature, Signature, 4) == 0) {
                if (ValidateACPITableChecksum(Table)) {
                    DEBUG(TEXT("[FindACPITable] Found table %.4s at physical 0x%08X, virtual 0x%08X"),
                          Signature, PhysicalAddress, TableAddress);
                    return Table;
                }
            }
        }
    }

    DEBUG(TEXT("[FindACPITable] Table %.4s not found"), Signature);
    return NULL;
}

/************************************************************************/

/**
 * @brief Parse Multiple APIC Description Table (MADT).
 * @return TRUE if MADT was found and parsed successfully, FALSE otherwise.
 */
BOOL ParseMADT(void) {
    DEBUG(TEXT("[ParseMADT] Enter"));

    // Find MADT table
    G_MADT = (LPACPI_MADT)FindACPITable(TEXT("APIC"));
    if (G_MADT == NULL) {
        DEBUG(TEXT("[ParseMADT] MADT table not found"));
        return FALSE;
    }

    DEBUG(TEXT("[ParseMADT] MADT found, Local APIC address: 0x%08X, Flags: 0x%08X"),
          G_MADT->LocalApicAddress, G_MADT->Flags);

    // Store Local APIC address
    G_AcpiConfig.LocalApicAddress = G_MADT->LocalApicAddress;

    // Parse MADT entries
    U32 EntryOffset = 0;
    U32 TotalLength = G_MADT->Header.Length - sizeof(ACPI_MADT);

    while (EntryOffset < TotalLength) {
        LPACPI_MADT_ENTRY_HEADER Entry = (LPACPI_MADT_ENTRY_HEADER)&G_MADT->InterruptController[EntryOffset];

        if (Entry->Length == 0) {
            DEBUG(TEXT("[ParseMADT] Invalid entry length 0"));
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
                    DEBUG(TEXT("[ParseMADT] Local APIC: ProcessorId=%d, ApicId=%d, Flags=0x%08X"),
                          LocalApic->ProcessorId, LocalApic->ApicId, LocalApic->Flags);
                }
                break;
            }

            case ACPI_MADT_TYPE_IO_APIC: {
                LPACPI_MADT_IO_APIC IoApic = (LPACPI_MADT_IO_APIC)Entry;
                if (G_AcpiConfig.IoApicCount < 8) {
                    G_IoApicInfo[G_AcpiConfig.IoApicCount].IoApicId = IoApic->IoApicId;
                    G_IoApicInfo[G_AcpiConfig.IoApicCount].IoApicAddress = IoApic->IoApicAddress;
                    G_IoApicInfo[G_AcpiConfig.IoApicCount].GlobalSystemInterruptBase = IoApic->GlobalSystemInterruptBase;
                    G_IoApicInfo[G_AcpiConfig.IoApicCount].MaxRedirectionEntry = 0; // Will be read from I/O APIC later
                    G_AcpiConfig.IoApicCount++;
                    DEBUG(TEXT("[ParseMADT] I/O APIC: Id=%d, Address=0x%08X, GSI Base=%d"),
                          IoApic->IoApicId, IoApic->IoApicAddress, IoApic->GlobalSystemInterruptBase);
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
                    DEBUG(TEXT("[ParseMADT] Interrupt Override: Bus=%d, Source=%d, GSI=%d, Flags=0x%04X"),
                          Override->Bus, Override->Source, Override->GlobalSystemInterrupt, Override->Flags);
                }
                break;
            }

            case ACPI_MADT_TYPE_LOCAL_APIC_NMI: {
                LPACPI_MADT_LOCAL_APIC_NMI LocalApicNmi = (LPACPI_MADT_LOCAL_APIC_NMI)Entry;
                DEBUG(TEXT("[ParseMADT] Local APIC NMI: ProcessorId=%d, Flags=0x%04X, LINT=%d"),
                      LocalApicNmi->ProcessorId, LocalApicNmi->Flags, LocalApicNmi->LocalApicLint);
                break;
            }

            default:
                DEBUG(TEXT("[ParseMADT] Unknown MADT entry type: %d"), Entry->Type);
                break;
        }

        EntryOffset += Entry->Length;
    }

    // Set configuration flags
    G_AcpiConfig.UseLocalApic = (G_AcpiConfig.LocalApicCount > 0);
    G_AcpiConfig.UseIoApic = (G_AcpiConfig.IoApicCount > 0);

    DEBUG(TEXT("[ParseMADT] Parsed successfully: %d Local APICs, %d I/O APICs, %d overrides"),
          G_AcpiConfig.LocalApicCount, G_AcpiConfig.IoApicCount, G_AcpiConfig.InterruptOverrideCount);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize ACPI and parse tables.
 * @return TRUE if ACPI is available and initialized successfully, FALSE otherwise.
 */
BOOL InitializeACPI(void) {
    DEBUG(TEXT("[InitializeACPI] Enter"));

    // Clear configuration
    MemorySet(&G_AcpiConfig, 0, sizeof(ACPI_CONFIG));

    // Find RSDP
    G_RSDP = FindRSDP();
    if (G_RSDP == NULL) {
        DEBUG(TEXT("[InitializeACPI] RSDP not found, ACPI not available"));
        return FALSE;
    }

    // Map and validate RSDT
    if (G_RSDP->RsdtAddress != 0) {
        DEBUG(TEXT("[InitializeACPI] RSDT physical address: 0x%08X"), G_RSDP->RsdtAddress);

        // Check if RSDT is in low memory (first 1MB) or needs mapping
        if (G_RSDP->RsdtAddress < 0x100000) {
            // RSDT is in low memory, use direct mapping
            G_RSDT = (LPACPI_RSDT)G_RSDP->RsdtAddress;
        } else {
            // RSDT is in high memory, needs to be mapped
            LINEAR MappedAddress = MapTempPhysicalPage(G_RSDP->RsdtAddress & ~0xFFF);
            if (MappedAddress != 0) {
                G_RSDT = (LPACPI_RSDT)(MappedAddress + (G_RSDP->RsdtAddress & 0xFFF));
                DEBUG(TEXT("[InitializeACPI] RSDT mapped to virtual address: 0x%08X"), (U32)G_RSDT);
            } else {
                DEBUG(TEXT("[InitializeACPI] Failed to map RSDT"));
                G_RSDT = NULL;
            }
        }

        SAFE_USE(G_RSDT) {
            if (!IsValidMemory((LINEAR)G_RSDT)) {
                DEBUG(TEXT("[InitializeACPI] RSDT not accessible in virtual memory"));
                G_RSDT = NULL;
            } else if (!ValidateACPITableChecksum(&G_RSDT->Header)) {
                DEBUG(TEXT("[InitializeACPI] RSDT checksum validation failed"));
                G_RSDT = NULL;
            } else {
                DEBUG(TEXT("[InitializeACPI] RSDT found and validated at 0x%08X"), (U32)G_RSDT);
            }
        }
    }

    // Map and validate XSDT if available (ACPI 2.0+)
    if (G_RSDP->Revision >= 2 && G_RSDP->XsdtAddress.LO != 0 && G_RSDP->XsdtAddress.HI == 0) {
        U32 XsdtPhysical = G_RSDP->XsdtAddress.LO;
        DEBUG(TEXT("[InitializeACPI] XSDT physical address: 0x%08X"), XsdtPhysical);

        // Check if XSDT is in low memory (first 1MB) or needs mapping
        if (XsdtPhysical < 0x100000) {
            // XSDT is in low memory, use direct mapping
            G_XSDT = (LPACPI_XSDT)XsdtPhysical;
        } else {
            // XSDT is in high memory, needs to be mapped
            LINEAR MappedAddress = MapTempPhysicalPage(XsdtPhysical & ~0xFFF);
            if (MappedAddress != 0) {
                G_XSDT = (LPACPI_XSDT)(MappedAddress + (XsdtPhysical & 0xFFF));
                DEBUG(TEXT("[InitializeACPI] XSDT mapped to virtual address: 0x%08X"), (U32)G_XSDT);
            } else {
                DEBUG(TEXT("[InitializeACPI] Failed to map XSDT"));
                G_XSDT = NULL;
            }
        }

        SAFE_USE(G_XSDT) {
            if (!IsValidMemory((LINEAR)G_XSDT)) {
                DEBUG(TEXT("[InitializeACPI] XSDT not accessible in virtual memory"));
                G_XSDT = NULL;
            } else if (!ValidateACPITableChecksum(&G_XSDT->Header)) {
                DEBUG(TEXT("[InitializeACPI] XSDT checksum validation failed"));
                G_XSDT = NULL;
            } else {
                DEBUG(TEXT("[InitializeACPI] XSDT found and validated at 0x%08X"), (U32)G_XSDT);
            }
        }
    }

    // Check if we have at least one valid table
    if (G_RSDT == NULL && G_XSDT == NULL) {
        DEBUG(TEXT("[InitializeACPI] No valid RSDT or XSDT found"));
        return FALSE;
    }

    // Parse MADT for APIC information
    if (!ParseMADT()) {
        DEBUG(TEXT("[InitializeACPI] Failed to parse MADT"));
        return FALSE;
    }

    G_AcpiConfig.Valid = TRUE;

    DEBUG(TEXT("[InitializeACPI] ACPI initialization completed successfully"));
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
 * @brief Map an interrupt using override table.
 * @param IRQ Original IRQ number.
 * @return Global System Interrupt number, or IRQ if no override exists.
 */
U32 MapInterrupt(U8 IRQ) {
    for (U32 i = 0; i < G_AcpiConfig.InterruptOverrideCount; i++) {
        if (G_InterruptOverrides[i].Bus == 0 && G_InterruptOverrides[i].Source == IRQ) {
            DEBUG(TEXT("[MapInterrupt] IRQ %d mapped to GSI %d"),
                  IRQ, G_InterruptOverrides[i].GlobalSystemInterrupt);
            return G_InterruptOverrides[i].GlobalSystemInterrupt;
        }
    }

    // No override found, return original IRQ
    return IRQ;
}

/************************************************************************/

/**
 * @brief Shutdown the system using ACPI.
 * This function attempts to put the system into ACPI sleep state S5 (power off).
 */
void ACPIShutdown(void) {
    DEBUG(TEXT("[ACPIShutdown] Enter"));

    // Check if ACPI is available
    if (!G_AcpiConfig.Valid) {
        DEBUG(TEXT("[ACPIShutdown] ACPI not available"));
        return;
    }

    // Find FADT table if not already found
    if (G_FADT == NULL) {
        G_FADT = (LPACPI_FADT)FindACPITable(TEXT("FACP"));
        if (G_FADT == NULL) {
            DEBUG(TEXT("[ACPIShutdown] FADT table not found"));
            return;
        }
        DEBUG(TEXT("[ACPIShutdown] FADT found at 0x%08X"), (U32)G_FADT);
    }

    // Check if PM1 control block is available
    if (G_FADT->Pm1aControlBlock == 0) {
        DEBUG(TEXT("[ACPIShutdown] PM1a control block not available"));
        return;
    }

    DEBUG(TEXT("[ACPIShutdown] PM1a control block at port 0x%04X"), G_FADT->Pm1aControlBlock);

    // For S5 sleep state, we need to set SLP_TYP to 111b (7) and SLP_EN to 1
    // The PM1 control register format:
    // Bit 13: SLP_EN (Sleep Enable)
    // Bits 10-12: SLP_TYP (Sleep Type)
    // For S5 (soft off), SLP_TYP should be 111b (7)
    U16 Pm1ControlValue = (7 << 10) | (1 << 13);  // SLP_TYP=7, SLP_EN=1

    DEBUG(TEXT("[ACPIShutdown] Writing 0x%04X to PM1a control register"), Pm1ControlValue);

    // Write to PM1a control register
    OutPortWord(G_FADT->Pm1aControlBlock, Pm1ControlValue);

    // If PM1b control block is also available, write to it as well
    if (G_FADT->Pm1bControlBlock != 0) {
        DEBUG(TEXT("[ACPIShutdown] Writing 0x%04X to PM1b control register at port 0x%04X"),
              Pm1ControlValue, G_FADT->Pm1bControlBlock);
        OutPortWord(G_FADT->Pm1bControlBlock, Pm1ControlValue);
    }

    // If we reach here, ACPI shutdown failed
    DEBUG(TEXT("[ACPIShutdown] ACPI shutdown failed, system still running"));

    // Try alternative shutdown methods as fallback
    DEBUG(TEXT("[ACPIShutdown] Attempting fallback shutdown methods"));

    // Try QEMU/Bochs specific shutdown
    OutPortWord(0x604, 0x2000);  // QEMU shutdown
    OutPortWord(0xB004, 0x2000); // Bochs shutdown

    DEBUG(TEXT("[ACPIShutdown] All shutdown methods failed"));
}
