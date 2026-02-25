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


    HID report descriptor helper

\************************************************************************/

#include "utils/HIDReport.h"
#include "CoreString.h"

/************************************************************************/

#define HID_ITEM_TYPE_MAIN 0
#define HID_ITEM_TYPE_GLOBAL 1
#define HID_ITEM_TYPE_LOCAL 2

#define HID_MAIN_ITEM_INPUT 8

#define HID_GLOBAL_ITEM_USAGE_PAGE 0
#define HID_GLOBAL_ITEM_REPORT_SIZE 7
#define HID_GLOBAL_ITEM_REPORT_ID 8
#define HID_GLOBAL_ITEM_REPORT_COUNT 9
#define HID_GLOBAL_ITEM_PUSH 10
#define HID_GLOBAL_ITEM_POP 11

#define HID_LOCAL_ITEM_USAGE 0
#define HID_LOCAL_ITEM_USAGE_MINIMUM 1
#define HID_LOCAL_ITEM_USAGE_MAXIMUM 2

/************************************************************************/

typedef struct tag_HID_GLOBAL_STATE {
    U32 UsagePage;
    U32 ReportSize;
    U32 ReportCount;
    U32 ReportId;
} HID_GLOBAL_STATE, *LPHID_GLOBAL_STATE;

typedef struct tag_HID_LOCAL_STATE {
    U32 Usages[HID_REPORT_MAX_FIELDS];
    UINT UsageCount;
    BOOL HasUsageRange;
    U32 UsageMinimum;
    U32 UsageMaximum;
} HID_LOCAL_STATE, *LPHID_LOCAL_STATE;

/************************************************************************/

static void HidReportResetLocalState(LPHID_LOCAL_STATE Local) {
    if (Local == NULL) {
        return;
    }

    Local->UsageCount = 0;
    Local->HasUsageRange = FALSE;
    Local->UsageMinimum = 0;
    Local->UsageMaximum = 0;
}

/************************************************************************/

static U32 HidReportReadUnsignedData(const U8 *Data, U8 Size) {
    U32 Value = 0;
    U8 Index = 0;

    if (Data == NULL) {
        return 0;
    }

    if (Size > 4) {
        Size = 4;
    }

    for (Index = 0; Index < Size; Index++) {
        Value |= ((U32)Data[Index] << (Index * 8));
    }

    return Value;
}

/************************************************************************/

static U16 HidReportToUsageValue(U32 Value) {
    if (Value > 0xFFFF) {
        return 0;
    }

    return (U16)Value;
}

/************************************************************************/

static U16 HidReportResolveUsage(const HID_LOCAL_STATE *Local, UINT Index) {
    if (Local == NULL) {
        return 0;
    }

    if (Local->UsageCount != 0) {
        UINT EffectiveIndex = Index;
        if (EffectiveIndex >= Local->UsageCount) {
            EffectiveIndex = Local->UsageCount - 1;
        }
        return HidReportToUsageValue(Local->Usages[EffectiveIndex]);
    }

    if (Local->HasUsageRange) {
        U32 Value = Local->UsageMinimum + Index;
        if (Value > Local->UsageMaximum) {
            Value = Local->UsageMaximum;
        }
        return HidReportToUsageValue(Value);
    }

    return 0;
}

/************************************************************************/

static BOOL HidReportAppendField(LPHID_REPORT_LAYOUT Layout,
                                 U16 UsagePage,
                                 U16 Usage,
                                 U16 UsageMinimum,
                                 U16 UsageMaximum,
                                 U16 BitOffset,
                                 U8 BitSize,
                                 U8 ReportCount,
                                 U8 ReportId,
                                 BOOL IsArray) {
    HID_REPORT_FIELD *Field = NULL;

    if (Layout == NULL || Layout->Fields == NULL) {
        return FALSE;
    }

    if (Layout->FieldCount >= Layout->FieldCapacity) {
        return FALSE;
    }

    Field = &Layout->Fields[Layout->FieldCount];
    Field->UsagePage = UsagePage;
    Field->Usage = Usage;
    Field->UsageMinimum = UsageMinimum;
    Field->UsageMaximum = UsageMaximum;
    Field->BitOffset = BitOffset;
    Field->BitSize = BitSize;
    Field->ReportCount = ReportCount;
    Field->ReportId = ReportId;
    Field->IsArray = IsArray;

    Layout->FieldCount++;
    return TRUE;
}

/************************************************************************/

BOOL HidReportParseInputLayout(const U8 *Descriptor, U16 DescriptorLength, LPHID_REPORT_LAYOUT Layout) {
    U16 Offset = 0;
    U16 ReportBitCursor[0x100];
    HID_GLOBAL_STATE Global;
    HID_GLOBAL_STATE GlobalStack[4];
    UINT GlobalDepth = 0;
    HID_LOCAL_STATE Local;

    if (Descriptor == NULL || Layout == NULL || Layout->Fields == NULL || Layout->FieldCapacity == 0) {
        return FALSE;
    }

    Layout->FieldCount = 0;
    MemorySet(ReportBitCursor, 0, sizeof(ReportBitCursor));
    MemorySet(&Global, 0, sizeof(Global));
    MemorySet(&GlobalStack, 0, sizeof(GlobalStack));
    MemorySet(&Local, 0, sizeof(Local));

    while (Offset < DescriptorLength) {
        U8 Prefix = Descriptor[Offset];
        U8 ItemSizeCode = Prefix & 0x03;
        U8 ItemSize = 0;
        U8 ItemType = (Prefix >> 2) & 0x03;
        U8 ItemTag = (Prefix >> 4) & 0x0F;
        U16 ItemStart = Offset;
        U32 Value = 0;
        const U8 *Data = NULL;

        if (Prefix == 0xFE) {
            if ((Offset + 2) >= DescriptorLength) {
                return FALSE;
            }

            ItemSize = Descriptor[Offset + 1];
            Offset = (U16)(Offset + 3);
            if ((Offset + ItemSize) > DescriptorLength) {
                return FALSE;
            }
            Offset = (U16)(Offset + ItemSize);
            continue;
        }

        ItemSize = (ItemSizeCode == 3) ? 4 : ItemSizeCode;
        Offset = (U16)(Offset + 1);
        if ((Offset + ItemSize) > DescriptorLength) {
            return FALSE;
        }

        Data = &Descriptor[Offset];
        Value = HidReportReadUnsignedData(Data, ItemSize);
        Offset = (U16)(Offset + ItemSize);

        if (ItemType == HID_ITEM_TYPE_GLOBAL) {
            switch (ItemTag) {
                case HID_GLOBAL_ITEM_USAGE_PAGE:
                    Global.UsagePage = Value;
                    break;
                case HID_GLOBAL_ITEM_REPORT_SIZE:
                    Global.ReportSize = Value;
                    break;
                case HID_GLOBAL_ITEM_REPORT_ID:
                    Global.ReportId = Value & 0xFF;
                    if (Global.ReportId == 0) {
                        return FALSE;
                    }
                    break;
                case HID_GLOBAL_ITEM_REPORT_COUNT:
                    Global.ReportCount = Value;
                    break;
                case HID_GLOBAL_ITEM_PUSH:
                    if (GlobalDepth >= (sizeof(GlobalStack) / sizeof(GlobalStack[0]))) {
                        return FALSE;
                    }
                    GlobalStack[GlobalDepth] = Global;
                    GlobalDepth++;
                    break;
                case HID_GLOBAL_ITEM_POP:
                    if (GlobalDepth == 0) {
                        return FALSE;
                    }
                    GlobalDepth--;
                    Global = GlobalStack[GlobalDepth];
                    break;
            }

            continue;
        }

        if (ItemType == HID_ITEM_TYPE_LOCAL) {
            switch (ItemTag) {
                case HID_LOCAL_ITEM_USAGE:
                    if (Local.UsageCount < (sizeof(Local.Usages) / sizeof(Local.Usages[0]))) {
                        Local.Usages[Local.UsageCount] = Value;
                        Local.UsageCount++;
                    }
                    break;
                case HID_LOCAL_ITEM_USAGE_MINIMUM:
                    Local.HasUsageRange = TRUE;
                    Local.UsageMinimum = Value;
                    if (Local.UsageMaximum < Local.UsageMinimum) {
                        Local.UsageMaximum = Local.UsageMinimum;
                    }
                    break;
                case HID_LOCAL_ITEM_USAGE_MAXIMUM:
                    Local.HasUsageRange = TRUE;
                    Local.UsageMaximum = Value;
                    if (Local.UsageMinimum > Local.UsageMaximum) {
                        Local.UsageMinimum = Local.UsageMaximum;
                    }
                    break;
            }

            continue;
        }

        if (ItemType == HID_ITEM_TYPE_MAIN) {
            if (ItemTag == HID_MAIN_ITEM_INPUT) {
                U8 ReportId = (U8)(Global.ReportId & 0xFF);
                U16 BitOffset = ReportBitCursor[ReportId];
                BOOL IsConstant = (Value & BIT_0) != 0;
                BOOL IsVariable = (Value & BIT_1) != 0;

                if (Global.ReportSize == 0 || Global.ReportCount == 0 || Global.ReportSize > 32 ||
                    Global.ReportCount > 0xFF) {
                    return FALSE;
                }

                if (!IsConstant) {
                    if (IsVariable) {
                        UINT Index = 0;
                        for (Index = 0; Index < Global.ReportCount; Index++) {
                            if (!HidReportAppendField(Layout,
                                                      HidReportToUsageValue(Global.UsagePage),
                                                      HidReportResolveUsage(&Local, Index),
                                                      0,
                                                      0,
                                                      (U16)(BitOffset + (Index * Global.ReportSize)),
                                                      (U8)Global.ReportSize,
                                                      1,
                                                      ReportId,
                                                      FALSE)) {
                                return FALSE;
                            }
                        }
                    } else {
                        if (!HidReportAppendField(Layout,
                                                  HidReportToUsageValue(Global.UsagePage),
                                                  0,
                                                  HidReportToUsageValue(Local.UsageMinimum),
                                                  HidReportToUsageValue(Local.UsageMaximum),
                                                  BitOffset,
                                                  (U8)Global.ReportSize,
                                                  (U8)Global.ReportCount,
                                                  ReportId,
                                                  TRUE)) {
                            return FALSE;
                        }
                    }
                }

                ReportBitCursor[ReportId] = (U16)(BitOffset + (Global.ReportCount * Global.ReportSize));
            }

            HidReportResetLocalState(&Local);
            continue;
        }

        UNUSED(ItemStart);
    }

    return TRUE;
}

/************************************************************************/

BOOL HidReportReadUnsignedValue(const U8 *Report,
                                U16 ReportLength,
                                U8 ReportId,
                                U16 BitOffset,
                                U8 BitLength,
                                U32 *Value) {
    U32 Result = 0;
    U32 AbsoluteBitOffset = 0;
    U8 Index = 0;

    if (Report == NULL || Value == NULL || BitLength == 0 || BitLength > 32) {
        return FALSE;
    }

    if (ReportId != 0) {
        if (ReportLength == 0 || Report[0] != ReportId) {
            return FALSE;
        }
        AbsoluteBitOffset = 8 + BitOffset;
    } else {
        AbsoluteBitOffset = BitOffset;
    }

    if ((AbsoluteBitOffset + BitLength) > ((U32)ReportLength * 8)) {
        return FALSE;
    }

    for (Index = 0; Index < BitLength; Index++) {
        U32 CurrentBit = AbsoluteBitOffset + Index;
        U16 ByteIndex = (U16)(CurrentBit / 8);
        U8 BitIndex = (U8)(CurrentBit % 8);
        U8 BitValue = (Report[ByteIndex] >> BitIndex) & 0x01;

        Result |= ((U32)BitValue << Index);
    }

    *Value = Result;
    return TRUE;
}

/************************************************************************/

BOOL HidReportIsUsageActive(const HID_REPORT_LAYOUT *Layout,
                            const U8 *Report,
                            U16 ReportLength,
                            U16 UsagePage,
                            U16 Usage) {
    UINT Index = 0;
    U8 ElementIndex = 0;

    if (Layout == NULL || Layout->Fields == NULL || Report == NULL) {
        return FALSE;
    }

    for (Index = 0; Index < Layout->FieldCount; Index++) {
        const HID_REPORT_FIELD *Field = &Layout->Fields[Index];
        U32 Value = 0;

        if (Field->UsagePage != UsagePage) {
            continue;
        }

        if (!Field->IsArray) {
            if (Field->Usage != Usage) {
                continue;
            }

            if (!HidReportReadUnsignedValue(Report, ReportLength, Field->ReportId, Field->BitOffset, Field->BitSize, &Value)) {
                continue;
            }

            if (Value != 0) {
                return TRUE;
            }
            continue;
        }

        if (Field->ReportCount == 0 || Field->BitSize == 0) {
            continue;
        }

        if (Field->UsageMinimum != 0 || Field->UsageMaximum != 0) {
            if (Usage < Field->UsageMinimum || Usage > Field->UsageMaximum) {
                continue;
            }
        }

        for (ElementIndex = 0; ElementIndex < Field->ReportCount; ElementIndex++) {
            U16 ElementOffset = (U16)(Field->BitOffset + (ElementIndex * Field->BitSize));
            if (!HidReportReadUnsignedValue(Report,
                                            ReportLength,
                                            Field->ReportId,
                                            ElementOffset,
                                            Field->BitSize,
                                            &Value)) {
                continue;
            }

            if (Value == Usage) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/************************************************************************/

BOOL HidReportHasUsagePage(const HID_REPORT_LAYOUT *Layout, U16 UsagePage) {
    UINT Index = 0;

    if (Layout == NULL || Layout->Fields == NULL) {
        return FALSE;
    }

    for (Index = 0; Index < Layout->FieldCount; Index++) {
        if (Layout->Fields[Index].UsagePage == UsagePage) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/
