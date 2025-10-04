
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


    I/O APIC (Advanced Programmable Interrupt Controller)

\************************************************************************/

#include "Base.h"
#include "drivers/IOAPIC.h"
#include "ACPI.h"
#include "InterruptController.h"
#include "Memory.h"
#include "Log.h"
#include "String.h"

/***************************************************************************/

static IOAPIC_CONFIG g_IOAPICConfig = {0};

/***************************************************************************/

/**
 * @brief Initialize the I/O APIC subsystem.
 *
 * Discovers I/O APIC controllers through ACPI, maps their registers,
 * and performs initial configuration.
 *
 * @return TRUE if initialization successful, FALSE otherwise
 */
BOOL InitializeIOAPIC(void)
{
    LPACPI_CONFIG pACPIConfig;
    LPIO_APIC_INFO pIOAPICInfo;
    U32 i;
    U32 ControllerIndex = 0;

    DEBUG(TEXT("[InitializeIOAPIC] Initializing I/O APIC subsystem..."));

    // Get ACPI configuration
    pACPIConfig = GetACPIConfig();
    if (pACPIConfig == NULL || !pACPIConfig->Valid) {
        WARNING(TEXT("[InitializeIOAPIC] ACPI not available, cannot initialize I/O APIC"));
        return FALSE;
    }

    if (!pACPIConfig->UseIoApic || pACPIConfig->IoApicCount == 0) {
        DEBUG(TEXT("[InitializeIOAPIC] No I/O APIC found in ACPI tables"));
        return FALSE;
    }

    DEBUG(TEXT("[InitializeIOAPIC] Found %u I/O APIC controller(s) in ACPI"), pACPIConfig->IoApicCount);

    // Initialize each I/O APIC controller
    for (i = 0; i < pACPIConfig->IoApicCount && ControllerIndex < 8; i++) {
        pIOAPICInfo = GetIOApicInfo(i);
        if (pIOAPICInfo == NULL) {
            continue;
        }

        DEBUG(TEXT("[InitializeIOAPIC] Initializing controller %u: ID=%u, Address=%08X, GSI Base=%u"),
                  ControllerIndex, pIOAPICInfo->IoApicId, pIOAPICInfo->IoApicAddress,
                  pIOAPICInfo->GlobalSystemInterruptBase);

        // Store controller information
        g_IOAPICConfig.Controllers[ControllerIndex].IoApicId = pIOAPICInfo->IoApicId;
        g_IOAPICConfig.Controllers[ControllerIndex].PhysicalAddress = pIOAPICInfo->IoApicAddress;
        g_IOAPICConfig.Controllers[ControllerIndex].GlobalInterruptBase = pIOAPICInfo->GlobalSystemInterruptBase;
        g_IOAPICConfig.Controllers[ControllerIndex].Present = FALSE;

        // Map I/O APIC registers to virtual memory
        g_IOAPICConfig.Controllers[ControllerIndex].MappedAddress = MapIOMemory(
            pIOAPICInfo->IoApicAddress,
            N_4KB  // I/O APIC registers fit in one page
        );

        if (g_IOAPICConfig.Controllers[ControllerIndex].MappedAddress == 0) {
            DEBUG(TEXT("[InitializeIOAPIC] Failed to map controller %u registers"), ControllerIndex);
            continue;
        }

        DEBUG(TEXT("[InitializeIOAPIC] Controller %u mapped to virtual address %08X"),
                  ControllerIndex, g_IOAPICConfig.Controllers[ControllerIndex].MappedAddress);

        // Set Present flag to TRUE so ReadIOAPICRegister works
        g_IOAPICConfig.Controllers[ControllerIndex].Present = TRUE;

        // Temporarily set ControllerCount so ReadIOAPICRegister doesn't fail
        if (g_IOAPICConfig.ControllerCount <= ControllerIndex) {
            g_IOAPICConfig.ControllerCount = ControllerIndex + 1;
        }

        // Test basic connectivity first
        DEBUG(TEXT("[InitializeIOAPIC] Testing basic MMIO access to controller %u"), ControllerIndex);

        // Test direct MMIO access
        volatile U32* testPtr = (volatile U32*)g_IOAPICConfig.Controllers[ControllerIndex].MappedAddress;
        U32 directRead = *testPtr;
        DEBUG(TEXT("[InitializeIOAPIC] Direct MMIO read at %08X = %08X"),
              (U32)testPtr, directRead);

        // Try to read ID register first (register 0x00)
        DEBUG(TEXT("[InitializeIOAPIC] About to read ID register for controller %u"), ControllerIndex);
        U32 IDReg = ReadIOAPICRegister(ControllerIndex, IOAPIC_REG_ID);
        DEBUG(TEXT("[InitializeIOAPIC] Controller %u - ID register=%08X"), ControllerIndex, IDReg);

        // Read I/O APIC version and capabilities
        DEBUG(TEXT("[InitializeIOAPIC] About to read version register for controller %u"), ControllerIndex);
        U32 VersionReg = ReadIOAPICRegister(ControllerIndex, IOAPIC_REG_VER);
        DEBUG(TEXT("[InitializeIOAPIC] Controller %u - Version register read returned %08X"), ControllerIndex, VersionReg);

        DEBUG(TEXT("[InitializeIOAPIC] Controller %u - Raw version register=%08X"), ControllerIndex, VersionReg);

        g_IOAPICConfig.Controllers[ControllerIndex].Version = (U8)(VersionReg & IOAPIC_VER_VERSION_MASK);
        g_IOAPICConfig.Controllers[ControllerIndex].MaxRedirectionEntry =
            (U8)((VersionReg & IOAPIC_VER_MRE_MASK) >> IOAPIC_VER_MRE_SHIFT);

        DEBUG(TEXT("[InitializeIOAPIC] Controller %u - Version=%02X, Max Redirection Entry=%u"),
                  ControllerIndex, g_IOAPICConfig.Controllers[ControllerIndex].Version,
                  g_IOAPICConfig.Controllers[ControllerIndex].MaxRedirectionEntry);

        // Mask all interrupts during initialization
        MaskAllIOAPICInterrupts(ControllerIndex);

        // Present flag already set above
        g_IOAPICConfig.TotalInterrupts += g_IOAPICConfig.Controllers[ControllerIndex].MaxRedirectionEntry + 1;

        ControllerIndex++;
    }

    g_IOAPICConfig.ControllerCount = ControllerIndex;
    g_IOAPICConfig.NextFreeVector = IOAPIC_IRQ_BASE;

    if (g_IOAPICConfig.ControllerCount == 0) {
        DEBUG(TEXT("[InitializeIOAPIC] No I/O APIC controllers could be initialized"));
        return FALSE;
    }

    DEBUG(TEXT("[InitializeIOAPIC] Successfully initialized %u controller(s), %u total interrupts"),
              g_IOAPICConfig.ControllerCount, g_IOAPICConfig.TotalInterrupts);

    // Set up default configuration for standard PC interrupts
    SetDefaultIOAPICConfiguration();

    g_IOAPICConfig.Initialized = TRUE;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Shutdown the I/O APIC subsystem.
 *
 * Masks all interrupts and unmaps I/O APIC registers.
 */
void ShutdownIOAPIC(void)
{
    U32 i;

    if (!g_IOAPICConfig.Initialized) {
        return;
    }

    DEBUG(TEXT("[ShutdownIOAPIC] Shutting down I/O APIC subsystem..."));

    // Mask all interrupts on all controllers
    for (i = 0; i < g_IOAPICConfig.ControllerCount; i++) {
        if (g_IOAPICConfig.Controllers[i].Present) {
            MaskAllIOAPICInterrupts(i);

            // Unmap I/O APIC registers
            if (g_IOAPICConfig.Controllers[i].MappedAddress != 0) {
                UnMapIOMemory(g_IOAPICConfig.Controllers[i].MappedAddress, N_4KB);
                g_IOAPICConfig.Controllers[i].MappedAddress = 0;
            }
        }
    }

    g_IOAPICConfig.Initialized = FALSE;
    g_IOAPICConfig.ControllerCount = 0;
    g_IOAPICConfig.TotalInterrupts = 0;
    g_IOAPICConfig.NextFreeVector = 0;

    DEBUG(TEXT("[ShutdownIOAPIC] Shutdown complete"));
}

/***************************************************************************/

/**
 * @brief Read from an I/O APIC register.
 *
 * Uses indirect access: write register index to IOREGSEL, then read from IOWIN.
 *
 * @param ControllerIndex Index of the I/O APIC controller
 * @param Register Register index to read
 * @return Register value
 */
U32 ReadIOAPICRegister(U32 ControllerIndex, U8 Register)
{
    LINEAR BaseAddress;
    volatile U32* pRegSel;
    volatile U32* pIOWin;

    if (ControllerIndex >= g_IOAPICConfig.ControllerCount) {
        return 0;
    }

    if (!g_IOAPICConfig.Controllers[ControllerIndex].Present) {
        return 0;
    }

    BaseAddress = g_IOAPICConfig.Controllers[ControllerIndex].MappedAddress;
    if (BaseAddress == 0) {
        return 0;
    }

    pRegSel = (volatile U32*)(BaseAddress + IOAPIC_REGSEL);
    pIOWin = (volatile U32*)(BaseAddress + IOAPIC_IOWIN);

    // DEBUG(TEXT("[ReadIOAPICRegister] BaseAddr=%08X, RegSel=%08X, IOWin=%08X"), BaseAddress, (U32)pRegSel, (U32)pIOWin);

    // Write register index to IOREGSEL
    *pRegSel = Register;

    // DEBUG(TEXT("[ReadIOAPICRegister] Wrote register %02X to RegSel"), Register);

    // Read value from IOWIN
    U32 value = *pIOWin;

    // DEBUG(TEXT("[ReadIOAPICRegister] Read value %08X from IOWin"), value);

    return value;
}

/***************************************************************************/

/**
 * @brief Write to an I/O APIC register.
 *
 * Uses indirect access: write register index to IOREGSEL, then write to IOWIN.
 *
 * @param ControllerIndex Index of the I/O APIC controller
 * @param Register Register index to write
 * @param Value Value to write
 */
void WriteIOAPICRegister(U32 ControllerIndex, U8 Register, U32 Value)
{
    LINEAR BaseAddress;
    volatile U32* pRegSel;
    volatile U32* pIOWin;

    if (ControllerIndex >= g_IOAPICConfig.ControllerCount) {
        return;
    }

    if (!g_IOAPICConfig.Controllers[ControllerIndex].Present) {
        return;
    }

    BaseAddress = g_IOAPICConfig.Controllers[ControllerIndex].MappedAddress;
    if (BaseAddress == 0) {
        return;
    }

    pRegSel = (volatile U32*)(BaseAddress + IOAPIC_REGSEL);
    pIOWin = (volatile U32*)(BaseAddress + IOAPIC_IOWIN);

    // Write register index to IOREGSEL
    *pRegSel = Register;

    // Write value to IOWIN
    *pIOWin = Value;
}

/***************************************************************************/

/**
 * @brief Read a redirection table entry.
 *
 * Each redirection table entry is 64 bits, accessed as two 32-bit registers.
 *
 * @param ControllerIndex Index of the I/O APIC controller
 * @param Entry Entry number (0-based)
 * @param RedirectionEntry Pointer to structure to fill
 * @return TRUE if successful, FALSE otherwise
 */
BOOL ReadRedirectionEntry(U32 ControllerIndex, U8 Entry, LPIOAPIC_REDIRECTION_ENTRY RedirectionEntry)
{
    if (ControllerIndex >= g_IOAPICConfig.ControllerCount) {
        return FALSE;
    }

    if (!g_IOAPICConfig.Controllers[ControllerIndex].Present) {
        return FALSE;
    }

    if (Entry > g_IOAPICConfig.Controllers[ControllerIndex].MaxRedirectionEntry) {
        return FALSE;
    }

    if (RedirectionEntry == NULL) {
        return FALSE;
    }

    // Read low 32 bits
    RedirectionEntry->Low = ReadIOAPICRegister(ControllerIndex, IOAPIC_REG_REDTBL_BASE + (Entry * 2));

    // Read high 32 bits
    RedirectionEntry->High = ReadIOAPICRegister(ControllerIndex, IOAPIC_REG_REDTBL_BASE + (Entry * 2) + 1);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Write a redirection table entry.
 *
 * Each redirection table entry is 64 bits, accessed as two 32-bit registers.
 * The high 32 bits should be written first to avoid spurious interrupts.
 *
 * @param ControllerIndex Index of the I/O APIC controller
 * @param Entry Entry number (0-based)
 * @param RedirectionEntry Pointer to redirection entry structure
 * @return TRUE if successful, FALSE otherwise
 */
BOOL WriteRedirectionEntry(U32 ControllerIndex, U8 Entry, LPIOAPIC_REDIRECTION_ENTRY RedirectionEntry)
{
    if (ControllerIndex >= g_IOAPICConfig.ControllerCount) {
        return FALSE;
    }

    if (!g_IOAPICConfig.Controllers[ControllerIndex].Present) {
        return FALSE;
    }

    if (Entry > g_IOAPICConfig.Controllers[ControllerIndex].MaxRedirectionEntry) {
        return FALSE;
    }

    if (RedirectionEntry == NULL) {
        return FALSE;
    }

    // Write high 32 bits first to avoid spurious interrupts
    WriteIOAPICRegister(ControllerIndex, IOAPIC_REG_REDTBL_BASE + (Entry * 2) + 1, RedirectionEntry->High);

    // Write low 32 bits
    WriteIOAPICRegister(ControllerIndex, IOAPIC_REG_REDTBL_BASE + (Entry * 2), RedirectionEntry->Low);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Configure an I/O APIC interrupt.
 *
 * Maps a legacy IRQ to an I/O APIC redirection entry and configures it.
 *
 * @param IRQ Legacy IRQ number (0-15)
 * @param Vector Interrupt vector to assign
 * @param DeliveryMode Delivery mode (IOAPIC_REDTBL_DELMOD_*)
 * @param TriggerMode Trigger mode (0=Edge, 1=Level)
 * @param Polarity Polarity (0=Active High, 1=Active Low)
 * @param DestCPU Destination CPU APIC ID
 * @return TRUE if successfully configured, FALSE otherwise
 */
BOOL ConfigureIOAPICInterrupt(U8 IRQ, U8 Vector, U32 DeliveryMode, U8 TriggerMode, U8 Polarity, U8 DestCPU)
{
    U32 ControllerIndex;
    U8 Entry;
    IOAPIC_REDIRECTION_ENTRY RedirEntry = {0};
    LPINTERRUPT_OVERRIDE_INFO pOverride;
    U32 i;
    U32 MappedIRQ = IRQ;

    // Check for interrupt source overrides in ACPI
    LPACPI_CONFIG pACPIConfig = GetACPIConfig();
    if (pACPIConfig != NULL && pACPIConfig->Valid) {
        for (i = 0; i < pACPIConfig->InterruptOverrideCount; i++) {
            pOverride = GetInterruptOverrideInfo(i);
            if (pOverride != NULL && pOverride->Source == IRQ) {
                MappedIRQ = pOverride->GlobalSystemInterrupt;
                DEBUG(TEXT("[ConfigureIOAPICInterrupt] IRQ %u overridden to GSI %u"), IRQ, MappedIRQ);
                break;
            }
        }
    }

    // Find the I/O APIC controller and entry for this IRQ
    if (!MapIRQToIOAPIC(MappedIRQ, &ControllerIndex, &Entry)) {
        DEBUG(TEXT("[ConfigureIOAPICInterrupt] Cannot map IRQ %u (GSI %u) to I/O APIC"), IRQ, MappedIRQ);
        return FALSE;
    }

    // Configure redirection entry
    RedirEntry.Vector = Vector;
    RedirEntry.DeliveryMode = (DeliveryMode & IOAPIC_REDTBL_DELMOD_MASK) >> 8;
    RedirEntry.DestMode = 0;  // Physical destination mode
    RedirEntry.DeliveryStatus = 0;  // Read-only field
    RedirEntry.IntPolarity = Polarity;
    RedirEntry.RemoteIRR = 0;  // Read-only field
    RedirEntry.TriggerMode = TriggerMode;
    RedirEntry.Mask = 0;  // Enable interrupt
    RedirEntry.Destination = DestCPU;

    DEBUG(TEXT("[ConfigureIOAPICInterrupt] Configuring IRQ %u -> Vector %02X (Controller %u, Entry %u)"),
              IRQ, Vector, ControllerIndex, Entry);

    return WriteRedirectionEntry(ControllerIndex, Entry, &RedirEntry);
}

/***************************************************************************/

/**
 * @brief Enable an I/O APIC interrupt.
 *
 * Unmasks the specified IRQ in the I/O APIC redirection table.
 *
 * @param IRQ Legacy IRQ number to enable
 * @return TRUE if successfully enabled, FALSE otherwise
 */
BOOL EnableIOAPICInterrupt(U8 IRQ)
{
    U32 ControllerIndex;
    U8 Entry;
    IOAPIC_REDIRECTION_ENTRY RedirEntry;
    U32 MappedIRQ = MapInterrupt(IRQ);

    // DEBUG(TEXT("[EnableIOAPICInterrupt] called for IRQ %u, mapped to GSI %u"), IRQ, MappedIRQ);

    if (!MapIRQToIOAPIC(MappedIRQ, &ControllerIndex, &Entry)) {
        DEBUG(TEXT("[EnableIOAPICInterrupt] MapIRQToIOAPIC failed for GSI %u"), MappedIRQ);
        return FALSE;
    }

    // DEBUG(TEXT("[EnableIOAPICInterrupt] Mapped GSI %u to controller %u, entry %u"), MappedIRQ, ControllerIndex, Entry);

    if (!ReadRedirectionEntry(ControllerIndex, Entry, &RedirEntry)) {
        DEBUG(TEXT("[EnableIOAPICInterrupt] ReadRedirectionEntry failed for controller %u, entry %u"), ControllerIndex, Entry);
        return FALSE;
    }

    RedirEntry.Mask = 0;  // Unmask interrupt

    // DEBUG(TEXT("[EnableIOAPICInterrupt] Enabling IRQ %u (GSI %u)"), IRQ, MappedIRQ);
    return WriteRedirectionEntry(ControllerIndex, Entry, &RedirEntry);
}

/***************************************************************************/

/**
 * @brief Disable an I/O APIC interrupt.
 *
 * Masks the specified IRQ in the I/O APIC redirection table.
 *
 * @param IRQ Legacy IRQ number to disable
 * @return TRUE if successfully disabled, FALSE otherwise
 */
BOOL DisableIOAPICInterrupt(U8 IRQ)
{
    U32 ControllerIndex;
    U8 Entry;
    IOAPIC_REDIRECTION_ENTRY RedirEntry;
    U32 MappedIRQ = MapInterrupt(IRQ);

    if (!MapIRQToIOAPIC(MappedIRQ, &ControllerIndex, &Entry)) {
        return FALSE;
    }

    if (!ReadRedirectionEntry(ControllerIndex, Entry, &RedirEntry)) {
        return FALSE;
    }

    RedirEntry.Mask = 1;  // Mask interrupt

    // DEBUG(TEXT("[DisableIOAPICInterrupt] Disabling IRQ %u (GSI %u)"), IRQ, MappedIRQ);
    return WriteRedirectionEntry(ControllerIndex, Entry, &RedirEntry);
}

/***************************************************************************/

/**
 * @brief Mask all I/O APIC interrupts.
 *
 * Sets the mask bit on all redirection table entries for the specified controller.
 *
 * @param ControllerIndex Index of the I/O APIC controller
 */
void MaskAllIOAPICInterrupts(U32 ControllerIndex)
{
    U8 Entry;
    IOAPIC_REDIRECTION_ENTRY RedirEntry;

    if (ControllerIndex >= g_IOAPICConfig.ControllerCount) {
        return;
    }

    if (!g_IOAPICConfig.Controllers[ControllerIndex].Present) {
        return;
    }

    // DEBUG(TEXT("[MaskAllIOAPICInterrupts] Masking all interrupts on controller %u"), ControllerIndex);

    for (Entry = 0; Entry <= g_IOAPICConfig.Controllers[ControllerIndex].MaxRedirectionEntry; Entry++) {
        // Read current entry
        if (ReadRedirectionEntry(ControllerIndex, Entry, &RedirEntry)) {
            // Set mask bit
            RedirEntry.Mask = 1;

            // Write back the entry
            WriteRedirectionEntry(ControllerIndex, Entry, &RedirEntry);
        }
    }
}

/***************************************************************************/

/**
 * @brief Get I/O APIC configuration.
 *
 * @return Pointer to I/O APIC configuration structure
 */
LPIOAPIC_CONFIG GetIOAPICConfig(void)
{
    return &g_IOAPICConfig;
}

/***************************************************************************/

/**
 * @brief Get I/O APIC controller information.
 *
 * @param ControllerIndex Index of the I/O APIC controller
 * @return Pointer to I/O APIC controller structure, NULL if invalid index
 */
LPIOAPIC_CONTROLLER GetIOAPICController(U32 ControllerIndex)
{
    if (ControllerIndex >= g_IOAPICConfig.ControllerCount) {
        return NULL;
    }

    if (!g_IOAPICConfig.Controllers[ControllerIndex].Present) {
        return NULL;
    }

    return &g_IOAPICConfig.Controllers[ControllerIndex];
}

/***************************************************************************/

/**
 * @brief Map IRQ number to I/O APIC controller and entry.
 *
 * @param IRQ Legacy IRQ number or Global System Interrupt
 * @param ControllerIndex Pointer to store controller index
 * @param Entry Pointer to store redirection entry index
 * @return TRUE if mapping found, FALSE otherwise
 */
BOOL MapIRQToIOAPIC(U8 IRQ, U32* ControllerIndex, U8* Entry)
{
    U32 i;
    U32 GlobalInterrupt;

    if (ControllerIndex == NULL || Entry == NULL) {
        return FALSE;
    }

    // Find which I/O APIC handles this global system interrupt
    for (i = 0; i < g_IOAPICConfig.ControllerCount; i++) {
        if (!g_IOAPICConfig.Controllers[i].Present) {
            continue;
        }

        GlobalInterrupt = g_IOAPICConfig.Controllers[i].GlobalInterruptBase;

        // Check if this IRQ falls within this controller's range
        if (IRQ >= GlobalInterrupt &&
            IRQ <= (GlobalInterrupt + g_IOAPICConfig.Controllers[i].MaxRedirectionEntry)) {

            *ControllerIndex = i;
            *Entry = IRQ - GlobalInterrupt;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Allocate next available interrupt vector.
 *
 * @return Interrupt vector number, 0 if none available
 */
U8 AllocateInterruptVector(void)
{
    if (g_IOAPICConfig.NextFreeVector > 0xFE) {
        return 0;  // No more vectors available
    }

    return g_IOAPICConfig.NextFreeVector++;
}

/***************************************************************************/

/**
 * @brief Set default I/O APIC configuration for standard PC interrupts.
 *
 * Configures standard IRQs (timer, keyboard, etc.) with appropriate settings.
 */
void SetDefaultIOAPICConfiguration(void)
{
    U8 StandardIRQs[] = {0, 1, 3, 4, 7, 8, 12, 14, 15}; // Standard PC IRQs to configure
    U8 NumIRQs = sizeof(StandardIRQs) / sizeof(StandardIRQs[0]);
    U8 i, Vector, ActualPin, TriggerMode, Polarity;

    // DEBUG(TEXT("[SetDefaultIOAPICConfiguration] Setting up default interrupt configuration with IRQ mapping"));

    for (i = 0; i < NumIRQs; i++) {
        U8 IRQ = StandardIRQs[i];

        // Get the actual pin mapping for this IRQ (may be overridden by ACPI)
        if (MapLegacyIRQ(IRQ, &ActualPin, &TriggerMode, &Polarity)) {
            // Use PIC-compatible vectors: 0x20 + IRQ (like traditional PIC)
            Vector = IOAPIC_IRQ_BASE + IRQ;

            // DEBUG(TEXT("[SetDefaultIOAPICConfiguration] Configuring IRQ %u -> Pin %u, Vector %02X, Trigger=%u, Polarity=%u"), IRQ, ActualPin, Vector, TriggerMode, Polarity);

            ConfigureIOAPICInterrupt(IRQ, Vector, IOAPIC_REDTBL_DELMOD_FIXED,
                                   TriggerMode, Polarity, 0);
        } else {
            ERROR(TEXT("[SetDefaultIOAPICConfiguration] Failed to map IRQ %u"), IRQ);
        }
    }

    // DEBUG(TEXT("[SetDefaultIOAPICConfiguration] Default configuration complete, next free vector = %02X"), g_IOAPICConfig.NextFreeVector);
}
