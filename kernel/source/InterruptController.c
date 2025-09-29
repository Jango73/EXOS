
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


    Interrupt Controller Abstraction Layer

\************************************************************************/

#include "../include/Base.h"
#include "../include/Kernel.h"
#include "../include/InterruptController.h"
#include "../include/LocalAPIC.h"
#include "../include/Log.h"
#include "../include/IOAPIC.h"
#include "../include/ACPI.h"
#include "../include/System.h"

/************************************************************************/
// Global interrupt controller configuration

static INTERRUPT_CONTROLLER_CONFIG g_InterruptControllerConfig;

/************************************************************************/
// PIC 8259 constants and functions

#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

#define PIC_EOI         0x20

// PIC initialization command words
#define ICW1_ICW4       0x01    // ICW4 (not) needed
#define ICW1_SINGLE     0x02    // Single (cascade) mode
#define ICW1_INTERVAL4  0x04    // Call address interval 4 (8)
#define ICW1_LEVEL      0x08    // Level triggered (edge) mode
#define ICW1_INIT       0x10    // Initialization - required!

#define ICW4_8086       0x01    // 8086/88 (MCS-80/85) mode
#define ICW4_AUTO       0x02    // Auto (normal) EOI
#define ICW4_BUF_SLAVE  0x08    // Buffered mode/slave
#define ICW4_BUF_MASTER 0x0C    // Buffered mode/master
#define ICW4_SFNM       0x10    // Special fully nested (not)

/************************************************************************/

/**
 * @brief Read PIC mask register
 *
 * @param PicNumber PIC number (1 or 2)
 * @return Current mask value
 */
static U8 ReadPICMask(U8 PicNumber) {
    if (PicNumber == 1) {
        return InPortByte(PIC1_DATA);
    } else if (PicNumber == 2) {
        return InPortByte(PIC2_DATA);
    }
    return 0xFF;
}

/************************************************************************/

/**
 * @brief Write PIC mask register
 *
 * @param PicNumber PIC number (1 or 2)
 * @param Mask Mask value to write
 */
static void WritePICMask(U8 PicNumber, U8 Mask) {
    if (PicNumber == 1) {
        OutPortByte(PIC1_DATA, Mask);
    } else if (PicNumber == 2) {
        OutPortByte(PIC2_DATA, Mask);
    }
}

/************************************************************************/

/**
 * @brief Disable PIC 8259 controllers
 */
static void DisablePIC8259(void) {
    // Save current masks
    g_InterruptControllerConfig.PICBaseMask = ReadPICMask(1);

    // Mask all interrupts on both PICs
    WritePICMask(1, 0xFF);
    WritePICMask(2, 0xFF);

    DEBUG(TEXT("[DisablePIC8259] PIC 8259 controllers disabled"));
}

/************************************************************************/

/**
 * @brief Initialize default IRQ mappings (1:1 mapping for PIC mode)
 */
static void InitializeDefaultIRQMappings(void) {
    U8 i;

    for (i = 0; i < 16; i++) {
        g_InterruptControllerConfig.IRQMappings[i].LegacyIRQ = i;
        g_InterruptControllerConfig.IRQMappings[i].ActualPin = i;
        g_InterruptControllerConfig.IRQMappings[i].TriggerMode = 0; // Edge-triggered
        g_InterruptControllerConfig.IRQMappings[i].Polarity = 0;    // Active high
        g_InterruptControllerConfig.IRQMappings[i].Override = FALSE;
    }
}

/************************************************************************/

/**
 * @brief Detect available interrupt controllers
 */
static void DetectInterruptControllers(void) {
    LPLOCAL_APIC_CONFIG LocalApicConfig;
    LPIOAPIC_CONFIG IOApicConfig;

    // Always assume PIC is present (it's part of the chipset)
    g_InterruptControllerConfig.PICPresent = TRUE;

    // Check for Local APIC
    LocalApicConfig = GetLocalAPICConfig();
    if (LocalApicConfig && LocalApicConfig->Present) {
        // Check for I/O APIC
        IOApicConfig = GetIOAPICConfig();
        if (IOApicConfig && IOApicConfig->Initialized && IOApicConfig->ControllerCount > 0) {
            g_InterruptControllerConfig.IOAPICPresent = TRUE;
            DEBUG(TEXT("[DetectInterruptControllers] Detected I/O APIC with %u controllers"), IOApicConfig->ControllerCount);
        }
    }

    DEBUG(TEXT("[DetectInterruptControllers] PIC=%s, IOAPIC=%s"),
          g_InterruptControllerConfig.PICPresent ? "YES" : "NO",
          g_InterruptControllerConfig.IOAPICPresent ? "YES" : "NO");
}

/************************************************************************/

BOOL InitializeInterruptController(INTERRUPT_CONTROLLER_MODE RequestedMode) {
    // Initialize configuration structure
    MemorySet(&g_InterruptControllerConfig, 0, sizeof(INTERRUPT_CONTROLLER_CONFIG));
    g_InterruptControllerConfig.RequestedMode = RequestedMode;
    g_InterruptControllerConfig.ActiveType = INTCTRL_TYPE_NONE;
    g_InterruptControllerConfig.TransitionActive = FALSE;

    // Initialize default IRQ mappings
    InitializeDefaultIRQMappings();

    // Detect available interrupt controllers
    DetectInterruptControllers();

    // Determine which controller to use
    switch (RequestedMode) {
        case INTCTRL_MODE_FORCE_PIC:
            if (g_InterruptControllerConfig.PICPresent) {
                g_InterruptControllerConfig.ActiveType = INTCTRL_TYPE_PIC;
                DEBUG(TEXT("[InitializeInterruptController] Forced PIC 8259 mode"));
            } else {
                ERROR(TEXT("[InitializeInterruptController]  PIC 8259 forced but not available"));
                return FALSE;
            }
            break;

        case INTCTRL_MODE_FORCE_IOAPIC:
            if (g_InterruptControllerConfig.IOAPICPresent) {
                if (!TransitionToIOAPICMode()) {
                    ERROR(TEXT("[InitializeInterruptController]  Failed to transition to I/O APIC mode"));
                    return FALSE;
                }
            } else {
                ERROR(TEXT("[InitializeInterruptController]  I/O APIC forced but not available"));
                return FALSE;
            }
            break;

        case INTCTRL_MODE_AUTO:
        default:
            // Prefer I/O APIC if available, otherwise use PIC
            DEBUG(TEXT("[InitializeInterruptController] Auto mode - IOAPICPresent=%s"),
                  g_InterruptControllerConfig.IOAPICPresent ? "YES" : "NO");
            if (g_InterruptControllerConfig.IOAPICPresent) {
                DEBUG(TEXT("[InitializeInterruptController] Attempting transition to I/O APIC mode"));
                if (TransitionToIOAPICMode()) {
                    DEBUG(TEXT("[InitializeInterruptController] Automatically selected I/O APIC mode"));
                } else {
                    DEBUG(TEXT("[InitializeInterruptController] I/O APIC transition failed, falling back to PIC"));
                    g_InterruptControllerConfig.ActiveType = INTCTRL_TYPE_PIC;
                }
            } else {
                g_InterruptControllerConfig.ActiveType = INTCTRL_TYPE_PIC;
                DEBUG(TEXT("[InitializeInterruptController] Automatically selected PIC 8259 mode (no IOAPIC available)"));
            }
            break;
    }

    DEBUG(TEXT("[InitializeInterruptController] Interrupt controller initialized: Type=%s"),
          (g_InterruptControllerConfig.ActiveType == INTCTRL_TYPE_PIC) ? "PIC" :
          (g_InterruptControllerConfig.ActiveType == INTCTRL_TYPE_IOAPIC) ? "IOAPIC" : "NONE");

    return g_InterruptControllerConfig.ActiveType != INTCTRL_TYPE_NONE;
}

/************************************************************************/

void ShutdownInterruptController(void) {
    if (g_InterruptControllerConfig.ActiveType == INTCTRL_TYPE_IOAPIC) {
        ShutdownIOAPIC();
    }

    // Restore PIC if it was active
    if (g_InterruptControllerConfig.PICPresent) {
        WritePICMask(1, g_InterruptControllerConfig.PICBaseMask);
        WritePICMask(2, 0xFF);
    }

    MemorySet(&g_InterruptControllerConfig, 0, sizeof(INTERRUPT_CONTROLLER_CONFIG));
    DEBUG(TEXT("[ShutdownInterruptController] Interrupt controller subsystem shutdown"));
}

/************************************************************************/

LPINTERRUPT_CONTROLLER_CONFIG GetInterruptControllerConfig(void) {
    return &g_InterruptControllerConfig;
}

/************************************************************************/

INTERRUPT_CONTROLLER_TYPE GetActiveInterruptControllerType(void) {
    return g_InterruptControllerConfig.ActiveType;
}

/************************************************************************/

BOOL IsIOAPICModeActive(void) {
    return g_InterruptControllerConfig.ActiveType == INTCTRL_TYPE_IOAPIC;
}

/************************************************************************/

BOOL IsPICModeActive(void) {
    return g_InterruptControllerConfig.ActiveType == INTCTRL_TYPE_PIC;
}

/************************************************************************/

BOOL EnableInterrupt(U8 IRQ) {
    DEBUG(TEXT("[EnableInterrupt] IRQ %u"), IRQ);
    DEBUG(TEXT("[EnableInterrupt] IsIOAPICModeActive() = %s"), IsIOAPICModeActive() ? TEXT("TRUE") : TEXT("FALSE"));
    DEBUG(TEXT("[EnableInterrupt] IsPICModeActive() = %s"), IsPICModeActive() ? TEXT("TRUE") : TEXT("FALSE"));

    if (IsIOAPICModeActive()) {
        DEBUG(TEXT("[EnableInterrupt] Calling EnableIOAPICInterrupt for IRQ %u"), IRQ);
        return EnableIOAPICInterrupt(IRQ);
    } else if (IsPICModeActive()) {
        // Enable interrupt in PIC 8259
        U8 Mask;

        if (IRQ < 8) {
            Mask = ReadPICMask(1);
            Mask &= ~(1 << IRQ);
            WritePICMask(1, Mask);
        } else if (IRQ < 16) {
            Mask = ReadPICMask(2);
            Mask &= ~(1 << (IRQ - 8));
            WritePICMask(2, Mask);

            // Enable cascade interrupt (IRQ 2) on PIC1
            Mask = ReadPICMask(1);
            Mask &= ~(1 << 2);
            WritePICMask(1, Mask);
        } else {
            return FALSE;
        }
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL DisableInterrupt(U8 IRQ) {
    if (IsIOAPICModeActive()) {
        return DisableIOAPICInterrupt(IRQ);
    } else if (IsPICModeActive()) {
        // Disable interrupt in PIC 8259
        U8 Mask;

        if (IRQ < 8) {
            Mask = ReadPICMask(1);
            Mask |= (1 << IRQ);
            WritePICMask(1, Mask);
        } else if (IRQ < 16) {
            Mask = ReadPICMask(2);
            Mask |= (1 << (IRQ - 8));
            WritePICMask(2, Mask);
        } else {
            return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}

/************************************************************************/

void MaskAllInterrupts(void) {
    if (IsIOAPICModeActive()) {
        LPIOAPIC_CONFIG IOApicConfig = GetIOAPICConfig();
        U32 i;

        if (IOApicConfig) {
            for (i = 0; i < IOApicConfig->ControllerCount; i++) {
                MaskAllIOAPICInterrupts(i);
            }
        }
    } else if (IsPICModeActive()) {
        WritePICMask(1, 0xFF);
        WritePICMask(2, 0xFF);
    }
}

/************************************************************************/

void UnmaskAllInterrupts(void) {
    if (IsIOAPICModeActive()) {
        // Restore I/O APIC redirection table to enabled state
        // This would need to be implemented based on saved state
        DEBUG(TEXT("[UnmaskAllInterrupts] TODO: Implement I/O APIC unmask all"));
    } else if (IsPICModeActive()) {
        WritePICMask(1, g_InterruptControllerConfig.PICBaseMask);
        WritePICMask(2, 0xFF); // Keep PIC2 masked unless needed
    }
}

/************************************************************************/

void SendInterruptEOI(void) {
    if (IsIOAPICModeActive()) {
        // Send EOI to Local APIC
        SendLocalAPICEOI();
    } else if (IsPICModeActive()) {
        // Send EOI to PIC 8259
        OutPortByte(PIC1_COMMAND, PIC_EOI);
    }
}

/************************************************************************/

/**
 * @brief Test I/O APIC functionality to ensure it can be used
 *
 * @return TRUE if I/O APIC is functional, FALSE otherwise
 */
static BOOL TestIOAPICFunctionality(void) {
    LPIOAPIC_CONFIG IOApicConfig;
    U32 VersionReg;
    U8 MaxRedirEntries;
    U32 i;
    BOOL FoundFunctional = FALSE;

    DEBUG(TEXT("[TestIOAPICFunctionality] Starting I/O APIC functionality test"));

    // Get I/O APIC configuration
    IOApicConfig = GetIOAPICConfig();
    if (!IOApicConfig || !IOApicConfig->Initialized || IOApicConfig->ControllerCount == 0) {
        DEBUG(TEXT("[TestIOAPICFunctionality] I/O APIC config invalid"));
        return FALSE;
    }

    // Test all I/O APIC controllers to find at least one functional
    for (i = 0; i < IOApicConfig->ControllerCount; i++) {
        DEBUG(TEXT("[TestIOAPICFunctionality] Testing controller %u at mapped address %08X"),
              i, IOApicConfig->Controllers[i].MappedAddress);

        // Read version register to test MMIO access
        VersionReg = ReadIOAPICRegister(i, IOAPIC_REG_VER);
        DEBUG(TEXT("[TestIOAPICFunctionality] Controller %u: Version register = %08X"), i, VersionReg);

        // Check if version register returned invalid data (0x00000000 or 0xFFFFFFFF)
        if (VersionReg == 0x00000000 || VersionReg == 0xFFFFFFFF) {
            DEBUG(TEXT("[TestIOAPICFunctionality] Controller %u: Invalid version register - skipping"), i);
            continue;
        }

        // Extract maximum redirection entries
        MaxRedirEntries = (U8)((VersionReg >> 16) & 0xFF);
        DEBUG(TEXT("[TestIOAPICFunctionality] Controller %u: Max redirection entries = %u"), i, MaxRedirEntries);

        // Check if we have enough redirection entries for basic PC interrupts
        if (MaxRedirEntries < 15) {
            DEBUG(TEXT("[TestIOAPICFunctionality] Controller %u: Insufficient redirection entries (%u) - skipping"),
                  i, MaxRedirEntries);
            continue;
        }

        // Try to read ID register as another connectivity test
        U32 IdReg = ReadIOAPICRegister(i, IOAPIC_REG_ID);
        DEBUG(TEXT("[TestIOAPICFunctionality] Controller %u: ID register = %08X"), i, IdReg);

        // Only reject if we get 0xFFFFFFFF (hardware not responding)
        // ID register can legitimately be 0x00000000 (ID = 0)
        if (IdReg == 0xFFFFFFFF) {
            DEBUG(TEXT("[TestIOAPICFunctionality] Controller %u: Hardware not responding - skipping"), i);
            continue;
        }

        // This controller appears functional
        DEBUG(TEXT("[TestIOAPICFunctionality] Controller %u: Functional and ready"), i);
        FoundFunctional = TRUE;
    }

    if (FoundFunctional) {
        DEBUG(TEXT("[TestIOAPICFunctionality] At least one I/O APIC controller is functional"));
        return TRUE;
    } else {
        DEBUG(TEXT("[TestIOAPICFunctionality] No functional I/O APIC controllers found"));
        return FALSE;
    }
}

/************************************************************************/

BOOL TransitionToIOAPICMode(void) {
    if (!g_InterruptControllerConfig.IOAPICPresent) {
        DEBUG(TEXT("[TransitionToIOAPICMode] Cannot transition to I/O APIC mode: I/O APIC not present"));
        return FALSE;
    }

    g_InterruptControllerConfig.TransitionActive = TRUE;

    // Step 1: Set up IRQ mappings from ACPI
    if (!SetupIRQMappings()) {
        WARNING(TEXT("[TransitionToIOAPICMode] Could not set up IRQ mappings from ACPI"));
    }

    // Step 2: Test IOAPIC functionality before shutting down PIC
    DEBUG(TEXT("[TransitionToIOAPICMode] Testing I/O APIC functionality before transition"));
    if (!TestIOAPICFunctionality()) {
        ERROR(TEXT("[TransitionToIOAPICMode] I/O APIC functionality test failed - cannot transition"));
        g_InterruptControllerConfig.TransitionActive = FALSE;
        return FALSE;
    }

    // Step 3: Shutdown PIC 8259
    if (!ShutdownPIC8259()) {
        ERROR(TEXT("[TransitionToIOAPICMode] Failed to shutdown PIC 8259"));
        g_InterruptControllerConfig.TransitionActive = FALSE;
        return FALSE;
    }

    // Step 4: Configure I/O APIC for standard PC interrupts
    SetDefaultIOAPICConfiguration();

    // Step 5: Set active type
    g_InterruptControllerConfig.ActiveType = INTCTRL_TYPE_IOAPIC;
    g_InterruptControllerConfig.TransitionActive = FALSE;

    DEBUG(TEXT("[TransitionToIOAPICMode] Successfully transitioned to I/O APIC mode"));
    return TRUE;
}

/************************************************************************/

BOOL ShutdownPIC8259(void) {
    if (!g_InterruptControllerConfig.PICPresent) {
        return TRUE; // Nothing to shutdown
    }

    DEBUG(TEXT("[ShutdownPIC8259] Shutting down PIC 8259"));

    // Disable all PIC interrupts
    DisablePIC8259();

    // Send any pending EOIs to clear interrupt state
    OutPortByte(PIC1_COMMAND, PIC_EOI);
    OutPortByte(PIC2_COMMAND, PIC_EOI);

    // Small delay to ensure commands are processed
    InPortByte(0x80); // I/O delay
    InPortByte(0x80);

    DEBUG(TEXT("[ShutdownPIC8259] PIC 8259 shutdown complete"));
    return TRUE;
}

/************************************************************************/

BOOL SetupIRQMappings(void) {
    LPACPI_CONFIG AcpiConfig;
    LPINTERRUPT_OVERRIDE_INFO OverrideInfo;
    U32 i;

    // Initialize default 1:1 mappings
    InitializeDefaultIRQMappings();

    // Get ACPI configuration
    AcpiConfig = GetACPIConfig();
    if (!AcpiConfig || !AcpiConfig->Valid) {
        DEBUG(TEXT("[SetupIRQMappings] No ACPI configuration available, using default IRQ mappings"));
        return TRUE;
    }

    // Process interrupt source overrides from ACPI MADT
    DEBUG(TEXT("[SetupIRQMappings] Processing %u interrupt source overrides from ACPI"), AcpiConfig->InterruptOverrideCount);

    for (i = 0; i < AcpiConfig->InterruptOverrideCount; i++) {
        OverrideInfo = GetInterruptOverrideInfo(i);
        if (OverrideInfo && OverrideInfo->Bus == 0 && OverrideInfo->Source < 16) {
            // Only handle ISA bus (bus 0) overrides for IRQ 0-15
            U8 TriggerMode = (OverrideInfo->Flags & 0x0C) >> 2; // Bits 3:2
            U8 Polarity = (OverrideInfo->Flags & 0x03);         // Bits 1:0

            // Convert MPS INTI flags to our format
            if (TriggerMode == 0x01) TriggerMode = 0; // Edge-triggered
            else if (TriggerMode == 0x03) TriggerMode = 1; // Level-triggered
            else TriggerMode = 0; // Default to edge-triggered

            if (Polarity == 0x01) Polarity = 0; // Active high
            else if (Polarity == 0x03) Polarity = 1; // Active low
            else Polarity = 0; // Default to active high

            HandleInterruptSourceOverride(OverrideInfo->Source, OverrideInfo->GlobalSystemInterrupt,
                                        TriggerMode, Polarity);
        }
    }

    return TRUE;
}

/************************************************************************/

BOOL MapLegacyIRQ(U8 LegacyIRQ, U8* ActualPin, U8* TriggerMode, U8* Polarity) {
    if (LegacyIRQ >= 16) {
        return FALSE;
    }

    if (ActualPin) {
        *ActualPin = g_InterruptControllerConfig.IRQMappings[LegacyIRQ].ActualPin;
    }
    if (TriggerMode) {
        *TriggerMode = g_InterruptControllerConfig.IRQMappings[LegacyIRQ].TriggerMode;
    }
    if (Polarity) {
        *Polarity = g_InterruptControllerConfig.IRQMappings[LegacyIRQ].Polarity;
    }

    return TRUE;
}

/************************************************************************/

BOOL ConfigureInterrupt(U8 IRQ, U8 Vector, U8 DestCPU) {
    if (IsIOAPICModeActive()) {
        U8 ActualPin, TriggerMode, Polarity;

        if (!MapLegacyIRQ(IRQ, &ActualPin, &TriggerMode, &Polarity)) {
            return FALSE;
        }

        return ConfigureIOAPICInterrupt(ActualPin, Vector,
                                       IOAPIC_REDTBL_DELMOD_FIXED,
                                       TriggerMode, Polarity, DestCPU);
    } else if (IsPICModeActive()) {
        // PIC configuration is simpler - just enable the IRQ
        return EnableInterrupt(IRQ);
    }

    return FALSE;
}

/************************************************************************/

void HandleInterruptSourceOverride(U8 LegacyIRQ, U32 GlobalIRQ, U8 TriggerMode, U8 Polarity) {
    if (LegacyIRQ >= 16) {
        return;
    }

    DEBUG(TEXT("[HandleInterruptSourceOverride] IRQ override: Legacy IRQ %u -> Global IRQ %u, Trigger=%u, Polarity=%u"),
          LegacyIRQ, GlobalIRQ, TriggerMode, Polarity);

    g_InterruptControllerConfig.IRQMappings[LegacyIRQ].ActualPin = (U8)GlobalIRQ;
    g_InterruptControllerConfig.IRQMappings[LegacyIRQ].TriggerMode = TriggerMode;
    g_InterruptControllerConfig.IRQMappings[LegacyIRQ].Polarity = Polarity;
    g_InterruptControllerConfig.IRQMappings[LegacyIRQ].Override = TRUE;
}

/************************************************************************/

BOOL DetectInterruptConflicts(void) {
    // TODO: Implement interrupt conflict detection
    // Check for:
    // - Multiple devices on same IRQ line
    // - Incompatible trigger modes
    // - Polarity conflicts
    return FALSE;
}

/************************************************************************/

BOOL ResolveInterruptConflicts(void) {
    // TODO: Implement interrupt conflict resolution
    // Try to:
    // - Reassign conflicting IRQs to free lines
    // - Use different I/O APIC entries
    // - Apply workarounds for known conflicts
    return FALSE;
}

/************************************************************************/

BOOL GetInterruptStatistics(U8 IRQ, U32* Count, U32* LastTimestamp) {
    // TODO: Implement interrupt statistics tracking
    // This would require maintaining counters for each IRQ
    UNUSED(IRQ);
    if (Count) *Count = 0;
    if (LastTimestamp) *LastTimestamp = 0;
    return FALSE;
}

/************************************************************************/

/**
 * @brief Temporarily switch to PIC mode for real mode calls
 *
 * @return TRUE if switch successful, FALSE otherwise
 */
BOOL SwitchToPICForRealMode(void) {
    DEBUG(TEXT("[SwitchToPICForRealMode] Enter"));

    if (g_InterruptControllerConfig.ActiveType != INTCTRL_TYPE_IOAPIC) {
        DEBUG(TEXT("[SwitchToPICForRealMode] Already in PIC mode, nothing to do"));
        return TRUE;
    }

    // Mask all IOAPIC interrupts for all controllers
    for (U32 i = 0; i < GetIOAPICConfig()->ControllerCount; i++) {
        MaskAllIOAPICInterrupts(i);
    }

    // Initialize PIC 8259 for real mode
    // ICW1: Initialize with ICW4 needed
    OutPortByte(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    OutPortByte(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    // ICW2: Set interrupt vector offsets (0x08 for master, 0x70 for slave in real mode)
    OutPortByte(PIC1_DATA, 0x08);
    OutPortByte(PIC2_DATA, 0x70);

    // ICW3: Set cascade connection
    OutPortByte(PIC1_DATA, 0x04); // IRQ2 connects to slave
    OutPortByte(PIC2_DATA, 0x02); // Slave ID

    // ICW4: Set mode
    OutPortByte(PIC1_DATA, ICW4_8086);
    OutPortByte(PIC2_DATA, ICW4_8086);

    // Unmask some basic interrupts for real mode
    OutPortByte(PIC1_DATA, 0xFE); // Enable IRQ0 (timer) only
    OutPortByte(PIC2_DATA, 0xFF); // Disable all slave interrupts

    DEBUG(TEXT("[SwitchToPICForRealMode] PIC 8259 configured for real mode"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Restore IOAPIC mode after real mode calls
 *
 * @return TRUE if restore successful, FALSE otherwise
 */
BOOL RestoreIOAPICAfterRealMode(void) {
    DEBUG(TEXT("[RestoreIOAPICAfterRealMode] Enter"));

    if (g_InterruptControllerConfig.ActiveType != INTCTRL_TYPE_IOAPIC) {
        DEBUG(TEXT("[RestoreIOAPICAfterRealMode] Not in IOAPIC mode, nothing to restore"));
        return TRUE;
    }

    // Disable PIC 8259 again
    OutPortByte(PIC1_DATA, 0xFF);
    OutPortByte(PIC2_DATA, 0xFF);

    // Send EOI to clear any pending interrupts
    OutPortByte(PIC1_COMMAND, PIC_EOI);
    OutPortByte(PIC2_COMMAND, PIC_EOI);

    // Restore default IOAPIC configuration
    SetDefaultIOAPICConfiguration();

    DEBUG(TEXT("[RestoreIOAPICAfterRealMode] IOAPIC mode restored"));
    return TRUE;
}
