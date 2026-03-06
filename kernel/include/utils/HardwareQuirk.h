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


    Hardware quirk matcher

\************************************************************************/

#ifndef HARDWARE_QUIRK_H_INCLUDED
#define HARDWARE_QUIRK_H_INCLUDED

#include "Base.h"
#include "drivers/bus/PCI.h"

/************************************************************************/

#define HARDWARE_QUIRK_PARAM_COUNT 6
#define HARDWARE_QUIRK_PARAM_0 0x00000001
#define HARDWARE_QUIRK_PARAM_1 0x00000002
#define HARDWARE_QUIRK_PARAM_2 0x00000004
#define HARDWARE_QUIRK_PARAM_3 0x00000008
#define HARDWARE_QUIRK_PARAM_4 0x00000010
#define HARDWARE_QUIRK_PARAM_5 0x00000020

#define HARDWARE_QUIRK_ANY_REVISION 0xFF

/************************************************************************/

typedef struct tag_HARDWARE_QUIRK_PCI_RULE {
    U16 VendorID;
    U16 DeviceID;
    U8 BaseClass;
    U8 SubClass;
    U8 ProgIF;
    U8 RevisionMin;
    U8 RevisionMax;
    U8 Reserved;
    U32 QuirkFlags;
    U32 ParameterMask;
    U32 Parameters[HARDWARE_QUIRK_PARAM_COUNT];
} HARDWARE_QUIRK_PCI_RULE, *LPHARDWARE_QUIRK_PCI_RULE;

typedef struct tag_HARDWARE_QUIRK_PCI_RESULT {
    U32 QuirkFlags;
    U32 ParameterMask;
    U32 Parameters[HARDWARE_QUIRK_PARAM_COUNT];
    UINT MatchCount;
} HARDWARE_QUIRK_PCI_RESULT, *LPHARDWARE_QUIRK_PCI_RESULT;

/************************************************************************/

void HardwareQuirkPciResultReset(LPHARDWARE_QUIRK_PCI_RESULT Result);
BOOL HardwareQuirkPciMatchRule(const PCI_INFO* PciInfo, const HARDWARE_QUIRK_PCI_RULE* Rule);
BOOL HardwareQuirkResolvePci(const PCI_INFO* PciInfo,
                             const HARDWARE_QUIRK_PCI_RULE* Rules,
                             UINT RuleCount,
                             LPHARDWARE_QUIRK_PCI_RESULT ResultOut);

#endif  // HARDWARE_QUIRK_H_INCLUDED
