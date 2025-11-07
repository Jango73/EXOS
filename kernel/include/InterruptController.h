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

#ifndef INTERRUPT_CONTROLLER_H_INCLUDED
#define INTERRUPT_CONTROLLER_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#define INTERRUPT_VECTOR_NETWORK 48

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// Interrupt controller types

typedef enum tag_INTERRUPT_CONTROLLER_TYPE {
    INTCTRL_TYPE_NONE = 0,      // No interrupt controller detected
    INTCTRL_TYPE_PIC,           // Legacy PIC 8259
    INTCTRL_TYPE_IOAPIC         // I/O APIC with Local APIC
} INTERRUPT_CONTROLLER_TYPE;

/***************************************************************************/
// Interrupt controller mode selection

typedef enum tag_INTERRUPT_CONTROLLER_MODE {
    INTCTRL_MODE_AUTO = 0,      // Automatic detection (prefer IOAPIC if available)
    INTCTRL_MODE_FORCE_PIC,     // Force PIC 8259 mode
    INTCTRL_MODE_FORCE_IOAPIC   // Force I/O APIC mode
} INTERRUPT_CONTROLLER_MODE;

/***************************************************************************/
// IRQ mapping entry for handling source overrides

typedef struct tag_IRQ_MAPPING {
    U8      LegacyIRQ;          // Original IRQ number (0-15)
    U8      ActualPin;          // Actual interrupt pin/vector
    U8      TriggerMode;        // 0=Edge, 1=Level
    U8      Polarity;           // 0=Active High, 1=Active Low
    BOOL    Override;           // TRUE if this is an ACPI override
} IRQ_MAPPING, *LPIRQ_MAPPING;

/***************************************************************************/
// Interrupt controller configuration

typedef struct tag_INTERRUPT_CONTROLLER_CONFIG {
    INTERRUPT_CONTROLLER_TYPE   ActiveType;        // Currently active controller type
    INTERRUPT_CONTROLLER_MODE   RequestedMode;     // User-requested mode
    BOOL                        PICPresent;        // TRUE if PIC 8259 is present
    BOOL                        IOAPICPresent;     // TRUE if I/O APIC is present
    BOOL                        TransitionActive;  // TRUE during PIC->IOAPIC transition
    U8                          PICBaseMask;       // Original PIC mask before shutdown
    IRQ_MAPPING                 IRQMappings[16];   // IRQ to pin mappings
} INTERRUPT_CONTROLLER_CONFIG, *LPINTERRUPT_CONTROLLER_CONFIG;

/***************************************************************************/
// Function prototypes

/**
 * Initialize the interrupt controller abstraction layer
 * @param RequestedMode Requested interrupt controller mode
 * @return TRUE if initialization successful, FALSE otherwise
 */
BOOL InitializeInterruptController(INTERRUPT_CONTROLLER_MODE RequestedMode);

/**
 * Shutdown the interrupt controller subsystem
 */
void ShutdownInterruptController(void);

/**
 * Get the current interrupt controller configuration
 * @return Pointer to interrupt controller configuration
 */
LPINTERRUPT_CONTROLLER_CONFIG GetInterruptControllerConfig(void);

/**
 * Get the active interrupt controller type
 * @return Active interrupt controller type
 */
INTERRUPT_CONTROLLER_TYPE GetActiveInterruptControllerType(void);

/**
 * Check if I/O APIC mode is active
 * @return TRUE if I/O APIC is active, FALSE otherwise
 */
BOOL IsIOAPICModeActive(void);

/**
 * Check if PIC mode is active
 * @return TRUE if PIC is active, FALSE otherwise
 */
BOOL IsPICModeActive(void);

/**
 * Enable an interrupt
 * @param IRQ IRQ number to enable
 * @return TRUE if successful, FALSE otherwise
 */
BOOL EnableInterrupt(U8 IRQ);

/**
 * Disable an interrupt
 * @param IRQ IRQ number to disable
 * @return TRUE if successful, FALSE otherwise
 */
BOOL DisableInterrupt(U8 IRQ);

/**
 * Mask all interrupts
 */
void MaskAllInterrupts(void);

/**
 * Unmask all interrupts (restore previous state)
 */
void UnmaskAllInterrupts(void);

/**
 * Send End of Interrupt signal
 */
void SendInterruptEOI(void);

/**
 * Transition from PIC to I/O APIC mode
 * @return TRUE if transition successful, FALSE otherwise
 */
BOOL TransitionToIOAPICMode(void);

/**
 * Shutdown PIC 8259 and switch to I/O APIC
 * @return TRUE if shutdown successful, FALSE otherwise
 */
BOOL ShutdownPIC8259(void);

/**
 * Set up IRQ to pin mappings based on ACPI information
 * @return TRUE if setup successful, FALSE otherwise
 */
BOOL SetupIRQMappings(void);

/**
 * Map legacy IRQ to actual interrupt pin
 * @param LegacyIRQ Legacy IRQ number (0-15)
 * @param ActualPin Pointer to store actual pin number
 * @param TriggerMode Pointer to store trigger mode
 * @param Polarity Pointer to store polarity
 * @return TRUE if mapping found, FALSE otherwise
 */
BOOL MapLegacyIRQ(U8 LegacyIRQ, U8* ActualPin, U8* TriggerMode, U8* Polarity);

/**
 * Configure interrupt for specific IRQ
 * @param IRQ Legacy IRQ number
 * @param Vector Interrupt vector
 * @param DestCPU Destination CPU
 * @return TRUE if configuration successful, FALSE otherwise
 */
BOOL ConfigureInterrupt(U8 IRQ, U8 Vector, U8 DestCPU);

BOOL ConfigureNetworkInterrupt(U8 IRQ, U8 DestCPU);
BOOL EnableNetworkInterrupt(U8 IRQ);
BOOL DisableNetworkInterrupt(U8 IRQ);

/**
 * Handle interrupt source override from ACPI
 * @param LegacyIRQ Legacy IRQ number
 * @param GlobalIRQ Global system interrupt number
 * @param TriggerMode Trigger mode (0=Edge, 1=Level)
 * @param Polarity Polarity (0=Active High, 1=Active Low)
 */
void HandleInterruptSourceOverride(U8 LegacyIRQ, U32 GlobalIRQ, U8 TriggerMode, U8 Polarity);

/**
 * Detect interrupt conflicts
 * @return TRUE if conflicts detected, FALSE otherwise
 */
BOOL DetectInterruptConflicts(void);

/**
 * Resolve interrupt conflicts
 * @return TRUE if conflicts resolved, FALSE otherwise
 */
BOOL ResolveInterruptConflicts(void);

/**
 * Get interrupt statistics
 * @param IRQ IRQ number
 * @param Count Pointer to store interrupt count
 * @param LastTimestamp Pointer to store last interrupt timestamp
 * @return TRUE if statistics available, FALSE otherwise
 */
BOOL GetInterruptStatistics(U8 IRQ, U32* Count, U32* LastTimestamp);

/**
 * Temporarily switch to PIC mode for real mode calls
 * @return TRUE if switch successful, FALSE otherwise
 */
BOOL SwitchToPICForRealMode(void);

/**
 * Restore IOAPIC mode after real mode calls
 * @return TRUE if restore successful, FALSE otherwise
 */
BOOL RestoreIOAPICAfterRealMode(void);

/***************************************************************************/

#pragma pack(pop)

#endif // INTERRUPT_CONTROLLER_H_INCLUDED
