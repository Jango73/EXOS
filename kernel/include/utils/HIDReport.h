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

#ifndef HIDREPORT_H_INCLUDED
#define HIDREPORT_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#define HID_REPORT_MAX_FIELDS 64

/************************************************************************/

typedef struct tag_HID_REPORT_FIELD {
    U16 UsagePage;
    U16 Usage;
    U16 UsageMinimum;
    U16 UsageMaximum;
    U16 BitOffset;
    U8 BitSize;
    U8 ReportCount;
    U8 ReportId;
    BOOL IsArray;
} HID_REPORT_FIELD, *LPHID_REPORT_FIELD;

typedef struct tag_HID_REPORT_LAYOUT {
    HID_REPORT_FIELD *Fields;
    UINT FieldCount;
    UINT FieldCapacity;
} HID_REPORT_LAYOUT, *LPHID_REPORT_LAYOUT;

/************************************************************************/

BOOL HidReportParseInputLayout(const U8 *Descriptor, U16 DescriptorLength, LPHID_REPORT_LAYOUT Layout);
BOOL HidReportReadUnsignedValue(const U8 *Report,
                                U16 ReportLength,
                                U8 ReportId,
                                U16 BitOffset,
                                U8 BitLength,
                                U32 *Value);
BOOL HidReportIsUsageActive(const HID_REPORT_LAYOUT *Layout,
                            const U8 *Report,
                            U16 ReportLength,
                            U16 UsagePage,
                            U16 Usage);
BOOL HidReportHasUsagePage(const HID_REPORT_LAYOUT *Layout, U16 UsagePage);

/************************************************************************/

#endif  // HIDREPORT_H_INCLUDED
