
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


    Local APIC (Advanced Programmable Interrupt Controller)

\************************************************************************/

#include "drivers/ACPI.h"
#include "Base.h"
#include "drivers/LocalAPIC.h"
#include "Log.h"
#include "Memory.h"
#include "String.h"
#include "System.h"

/***************************************************************************/

typedef struct tag_CPUIDREGISTERS {
    U32 reg_EAX;
    U32 reg_EBX;
    U32 reg_ECX;
    U32 reg_EDX;
} CPUIDREGISTERS, *LPCPUIDREGISTERS;

/***************************************************************************/

static LOCAL_APIC_CONFIG g_LocalApicConfig = {0};

/***************************************************************************/

/**
 * @brief Initialize the Local APIC subsystem.
 *
 * Detects Local APIC presence, maps registers, and performs initial configuration.
 *
 * @return TRUE if initialization successful, FALSE otherwise
 */
BOOL InitializeLocalAPIC(void) {
    LPACPI_CONFIG AcpiConfig;
    U32 ApicBaseAddr;

    DEBUG(TEXT("[LocalAPIC] Initializing Local APIC..."));

    // Clear configuration
    MemorySet(&g_LocalApicConfig, 0, sizeof(LOCAL_APIC_CONFIG));

    // Check if Local APIC is present via CPUID
    if (!IsLocalAPICPresent()) {
        DEBUG(TEXT("[LocalAPIC] Local APIC not present on this processor"));
        return FALSE;
    }

    // Get ACPI configuration
    AcpiConfig = GetACPIConfig();
    if (AcpiConfig && AcpiConfig->Valid && AcpiConfig->UseLocalApic) {
        ApicBaseAddr = AcpiConfig->LocalApicAddress;
        DEBUG(TEXT("[LocalAPIC] Using ACPI-provided Local APIC address: 0x%08X"), ApicBaseAddr);
    } else {
        // Fall back to MSR
        ApicBaseAddr = GetLocalAPICBaseAddress();
        DEBUG(TEXT("[LocalAPIC] Using MSR-provided Local APIC address: 0x%08X"), ApicBaseAddr);
    }

    if (ApicBaseAddr == 0) {
        DEBUG(TEXT("[LocalAPIC] Invalid Local APIC base address"));
        return FALSE;
    }

    // Map the Local APIC registers using MapIOMemory
    g_LocalApicConfig.MappedAddress = MapIOMemory(ApicBaseAddr, PAGE_SIZE);
    if (g_LocalApicConfig.MappedAddress == 0) {
        DEBUG(TEXT("[LocalAPIC] Failed to map Local APIC registers"));
        return FALSE;
    }

    g_LocalApicConfig.BaseAddress = ApicBaseAddr;
    g_LocalApicConfig.Present = TRUE;

    // TODO: Enable Local APIC later when we're ready to replace PIC completely
    // For now, just detect and map but don't enable to avoid conflicts
    /*
    if (!EnableLocalAPIC()) {
        DEBUG(TEXT("[LocalAPIC] Failed to enable Local APIC"));
        UnMapIOMemory(g_LocalApicConfig.MappedAddress, PAGE_SIZE);
        return FALSE;
    }
    */
    DEBUG(TEXT("[LocalAPIC] Local APIC mapped but not enabled (avoiding PIC conflict)"));

    // Read Local APIC information
    U32 VersionReg = ReadLocalAPICRegister(LOCAL_APIC_VERSION);
    g_LocalApicConfig.Version = (U8)(VersionReg & 0xFF);
    g_LocalApicConfig.MaxLvtEntries = (U8)((VersionReg >> 16) & 0xFF) + 1;
    g_LocalApicConfig.ApicId = GetLocalAPICId();

    DEBUG(TEXT("[LocalAPIC] Local APIC initialized: ID=%u, Version=0x%02X, MaxLVT=%u"),
              g_LocalApicConfig.ApicId, g_LocalApicConfig.Version, g_LocalApicConfig.MaxLvtEntries);

    // TODO: Set spurious interrupt vector when we enable Local APIC
    /*
    if (!SetSpuriousInterruptVector(0xFF)) {
        DEBUG(TEXT("[LocalAPIC] Failed to set spurious interrupt vector"));
        return FALSE;
    }
    */

    g_LocalApicConfig.Enabled = FALSE;  // Not enabled yet to avoid PIC conflicts
    return TRUE;
}

/************************************************************************/

/**
 * @brief Check if Local APIC is present via CPUID.
 *
 * Uses CPUID instruction to determine if the processor supports Local APIC.
 *
 * @return TRUE if Local APIC is present, FALSE otherwise
 */
BOOL IsLocalAPICPresent(void) {
    CPUIDREGISTERS Regs[4];

    // Call CPUID with EAX=1 to get feature information
    GetCPUID(Regs);

    // Check APIC feature bit in EDX
    return (Regs[1].reg_EDX & INTEL_CPU_FEAT_APIC) ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Enable the Local APIC via MSR.
 *
 * Sets the APIC Global Enable bit in the IA32_APIC_BASE MSR.
 *
 * @return TRUE if successfully enabled, FALSE otherwise
 */
BOOL EnableLocalAPIC(void) {
    U32 ApicBaseLow, ApicBaseHigh;

    if (!IsLocalAPICPresent()) {
        return FALSE;
    }

    // Read current APIC base MSR
    ApicBaseLow = ReadMSR(IA32_APIC_BASE_MSR);
    ApicBaseHigh = 0; // We only handle 32-bit for now

    // Set the enable bit
    ApicBaseLow |= IA32_APIC_BASE_ENABLE;

    // Write back to MSR
    WriteMSR64(IA32_APIC_BASE_MSR, ApicBaseLow, ApicBaseHigh);

    DEBUG(TEXT("[LocalAPIC] Local APIC enabled via MSR"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Disable the Local APIC via MSR.
 *
 * Clears the APIC Global Enable bit in the IA32_APIC_BASE MSR.
 *
 * @return TRUE if successfully disabled, FALSE otherwise
 */
BOOL DisableLocalAPIC(void) {
    U32 ApicBaseLow, ApicBaseHigh;

    // Read current APIC base MSR
    ApicBaseLow = ReadMSR(IA32_APIC_BASE_MSR);
    ApicBaseHigh = 0;

    // Clear the enable bit
    ApicBaseLow &= ~IA32_APIC_BASE_ENABLE;

    // Write back to MSR
    WriteMSR64(IA32_APIC_BASE_MSR, ApicBaseLow, ApicBaseHigh);

    g_LocalApicConfig.Enabled = FALSE;
    DEBUG(TEXT("[LocalAPIC] Local APIC disabled via MSR"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Read Local APIC base address from MSR.
 *
 * Extracts the physical base address from the IA32_APIC_BASE MSR.
 *
 * @return Physical base address of Local APIC
 */
U32 GetLocalAPICBaseAddress(void) {
    U32 ApicBaseLow;

    if (!IsLocalAPICPresent()) {
        return 0;
    }

    ApicBaseLow = ReadMSR(IA32_APIC_BASE_MSR);
    return ApicBaseLow & IA32_APIC_BASE_ADDR_MASK;
}

/************************************************************************/

/**
 * @brief Set Local APIC base address via MSR.
 *
 * Sets the physical base address in the IA32_APIC_BASE MSR.
 *
 * @param BaseAddress Physical base address to set
 * @return TRUE if successfully set, FALSE otherwise
 */
BOOL SetLocalAPICBaseAddress(U32 BaseAddress) {
    U32 ApicBaseLow, ApicBaseHigh;

    if (!IsLocalAPICPresent()) {
        return FALSE;
    }

    // Read current MSR to preserve flags
    ApicBaseLow = ReadMSR(IA32_APIC_BASE_MSR);
    ApicBaseHigh = 0;

    // Clear address bits and set new address
    ApicBaseLow = (ApicBaseLow & ~IA32_APIC_BASE_ADDR_MASK) | (BaseAddress & IA32_APIC_BASE_ADDR_MASK);

    // Write back to MSR
    WriteMSR64(IA32_APIC_BASE_MSR, ApicBaseLow, ApicBaseHigh);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Read from a Local APIC register.
 *
 * Reads a 32-bit value from the specified Local APIC register.
 *
 * @param Register Register offset
 * @return Register value
 */
U32 ReadLocalAPICRegister(U32 Register) {
    if (!g_LocalApicConfig.Present || g_LocalApicConfig.MappedAddress == 0) {
        return 0;
    }

    // Local APIC registers are 32-bit aligned
    volatile U32* RegPtr = (volatile U32*)(g_LocalApicConfig.MappedAddress + Register);
    return *RegPtr;
}

/************************************************************************/

/**
 * @brief Write to a Local APIC register.
 *
 * Writes a 32-bit value to the specified Local APIC register.
 *
 * @param Register Register offset
 * @param Value Value to write
 */
void WriteLocalAPICRegister(U32 Register, U32 Value) {
    if (!g_LocalApicConfig.Present || g_LocalApicConfig.MappedAddress == 0) {
        return;
    }

    // Local APIC registers are 32-bit aligned
    volatile U32* RegPtr = (volatile U32*)(g_LocalApicConfig.MappedAddress + Register);
    *RegPtr = Value;
}

/************************************************************************/

/**
 * @brief Get Local APIC ID.
 *
 * Reads the Local APIC ID from the ID register.
 *
 * @return Local APIC ID
 */
U8 GetLocalAPICId(void) {
    U32 IdReg = ReadLocalAPICRegister(LOCAL_APIC_ID);
    return (U8)((IdReg >> 24) & 0xFF);
}

/************************************************************************/

/**
 * @brief Send End of Interrupt (EOI) signal.
 *
 * Sends EOI to the Local APIC to indicate interrupt handling completion.
 */
void SendLocalAPICEOI(void) {
    WriteLocalAPICRegister(LOCAL_APIC_EOI, 0);
}

/************************************************************************/

/**
 * @brief Set spurious interrupt vector.
 *
 * Configures the spurious interrupt vector and enables the Local APIC.
 *
 * @param Vector Spurious interrupt vector (0x20-0xFF)
 * @return TRUE if successfully set, FALSE otherwise
 */
BOOL SetSpuriousInterruptVector(U8 Vector) {
    U32 SpuriousReg;

    if (!g_LocalApicConfig.Present) {
        return FALSE;
    }

    if (Vector < 0x20) {
        DEBUG(TEXT("[LocalAPIC] Invalid spurious vector: 0x%02X (must be >= 0x20)"), Vector);
        return FALSE;
    }

    // Enable APIC and set spurious vector
    SpuriousReg = LOCAL_APIC_SPURIOUS_ENABLE | Vector;
    WriteLocalAPICRegister(LOCAL_APIC_SPURIOUS_IV, SpuriousReg);

    g_LocalApicConfig.SpuriousVector = Vector;
    DEBUG(TEXT("[LocalAPIC] Set spurious interrupt vector to 0x%02X"), Vector);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Configure a Local Vector Table (LVT) entry.
 *
 * Sets up an LVT register with the specified vector, delivery mode, and mask status.
 *
 * @param LvtRegister LVT register offset
 * @param Vector Interrupt vector
 * @param DeliveryMode Delivery mode
 * @param Masked TRUE to mask the interrupt, FALSE to unmask
 * @return TRUE if successfully configured, FALSE otherwise
 */
BOOL ConfigureLVTEntry(U32 LvtRegister, U8 Vector, U32 DeliveryMode, BOOL Masked) {
    U32 LvtValue;

    if (!g_LocalApicConfig.Present) {
        return FALSE;
    }

    // Build LVT value
    LvtValue = Vector | (DeliveryMode & LOCAL_APIC_LVT_DELIVERY_MASK);
    if (Masked) {
        LvtValue |= LOCAL_APIC_LVT_MASK;
    }

    WriteLocalAPICRegister(LvtRegister, LvtValue);
    DEBUG(TEXT("[LocalAPIC] Configured LVT register 0x%03X: Vector=0x%02X, Mode=0x%03X, Masked=%s"),
              LvtRegister, Vector, DeliveryMode, Masked ? TEXT("Yes") : TEXT("No"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Get Local APIC configuration.
 *
 * Returns pointer to the current Local APIC configuration structure.
 *
 * @return Pointer to Local APIC configuration structure
 */
LPLOCAL_APIC_CONFIG GetLocalAPICConfig(void) {
    return &g_LocalApicConfig;
}

/************************************************************************/

/**
 * @brief Read MSR (Model Specific Register).
 *
 * Reads a 32-bit value from the specified MSR. For 64-bit MSRs, only the low 32 bits are returned.
 *
 * @param Msr MSR index
 * @return MSR value (low 32 bits)
 */
U32 ReadMSR(U32 Msr) {
    U32 Low, High;

    __asm__ volatile (
        "rdmsr"
        : "=a" (Low), "=d" (High)
        : "c" (Msr)
    );

    return Low;
}

/************************************************************************/

/**
 * @brief Write MSR (Model Specific Register).
 *
 * Writes a 32-bit value to the specified MSR. High 32 bits are set to 0.
 *
 * @param Msr MSR index
 * @param Value Value to write
 */
void WriteMSR(U32 Msr, U32 Value) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c" (Msr), "a" (Value), "d" (0)
    );
}

/************************************************************************/

/**
 * @brief Write 64-bit MSR (Model Specific Register).
 *
 * Writes a 64-bit value to the specified MSR.
 *
 * @param Msr MSR index
 * @param ValueLow Low 32 bits
 * @param ValueHigh High 32 bits
 */
void WriteMSR64(U32 Msr, U32 ValueLow, U32 ValueHigh) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c" (Msr), "a" (ValueLow), "d" (ValueHigh)
    );
}
