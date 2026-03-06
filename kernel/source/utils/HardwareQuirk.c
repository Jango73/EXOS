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

#include "utils/HardwareQuirk.h"
#include "CoreString.h"

/************************************************************************/

/**
 * @brief Reset one PCI quirk resolution result.
 * @param Result Result pointer.
 */
void HardwareQuirkPciResultReset(LPHARDWARE_QUIRK_PCI_RESULT Result) {
    if (Result == NULL) {
        return;
    }

    MemorySet(Result, 0, sizeof(HARDWARE_QUIRK_PCI_RESULT));
}

/************************************************************************/

/**
 * @brief Check if one PCI quirk rule matches one PCI function.
 * @param PciInfo PCI function info.
 * @param Rule Quirk rule.
 * @return TRUE when this rule applies.
 */
BOOL HardwareQuirkPciMatchRule(const PCI_INFO* PciInfo, const HARDWARE_QUIRK_PCI_RULE* Rule) {
    if (PciInfo == NULL || Rule == NULL) {
        return FALSE;
    }

    if (Rule->VendorID != PCI_ANY_ID && Rule->VendorID != PciInfo->VendorID) {
        return FALSE;
    }
    if (Rule->DeviceID != PCI_ANY_ID && Rule->DeviceID != PciInfo->DeviceID) {
        return FALSE;
    }
    if (Rule->BaseClass != PCI_ANY_CLASS && Rule->BaseClass != PciInfo->BaseClass) {
        return FALSE;
    }
    if (Rule->SubClass != PCI_ANY_CLASS && Rule->SubClass != PciInfo->SubClass) {
        return FALSE;
    }
    if (Rule->ProgIF != PCI_ANY_CLASS && Rule->ProgIF != PciInfo->ProgIF) {
        return FALSE;
    }

    if (Rule->RevisionMin != HARDWARE_QUIRK_ANY_REVISION || Rule->RevisionMax != HARDWARE_QUIRK_ANY_REVISION) {
        if (PciInfo->Revision < Rule->RevisionMin || PciInfo->Revision > Rule->RevisionMax) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolve all matching PCI quirk rules for one PCI function.
 * @param PciInfo PCI function info.
 * @param Rules Rule table.
 * @param RuleCount Rule count.
 * @param ResultOut Aggregated result.
 * @return TRUE when at least one rule matched.
 */
BOOL HardwareQuirkResolvePci(const PCI_INFO* PciInfo,
                             const HARDWARE_QUIRK_PCI_RULE* Rules,
                             UINT RuleCount,
                             LPHARDWARE_QUIRK_PCI_RESULT ResultOut) {
    if (PciInfo == NULL || Rules == NULL || ResultOut == NULL || RuleCount == 0) {
        return FALSE;
    }

    HardwareQuirkPciResultReset(ResultOut);

    for (UINT RuleIndex = 0; RuleIndex < RuleCount; RuleIndex++) {
        const HARDWARE_QUIRK_PCI_RULE* Rule = &Rules[RuleIndex];

        if (!HardwareQuirkPciMatchRule(PciInfo, Rule)) {
            continue;
        }

        ResultOut->MatchCount++;
        ResultOut->QuirkFlags |= Rule->QuirkFlags;

        for (UINT ParamIndex = 0; ParamIndex < HARDWARE_QUIRK_PARAM_COUNT; ParamIndex++) {
            U32 ParamBit = (U32)(1 << ParamIndex);
            if ((Rule->ParameterMask & ParamBit) == 0) {
                continue;
            }
            ResultOut->ParameterMask |= ParamBit;
            ResultOut->Parameters[ParamIndex] = Rule->Parameters[ParamIndex];
        }
    }

    return (ResultOut->MatchCount != 0) ? TRUE : FALSE;
}
