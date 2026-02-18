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


    NTFS timestamp conversion

\************************************************************************/

#include "NTFS-Private.h"

/***************************************************************************/

static const U8 NtfsDaysPerMonth[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/***************************************************************************/

/**
 * @brief Determine whether a year is leap.
 *
 * @param Year Year value.
 * @return TRUE for leap years, FALSE otherwise.
 */
static BOOL NtfsIsLeapYear(U32 Year) {
    return (Year % 400 == 0) || ((Year % 4 == 0) && (Year % 100 != 0));
}

/***************************************************************************/

/**
 * @brief Return number of days in a month for a given year.
 *
 * @param Year Year value.
 * @param Month Month value in range 1..12.
 * @return Number of days for the requested month.
 */
static U32 NtfsGetDaysInMonth(U32 Year, U32 Month) {
    U32 Days = NtfsDaysPerMonth[Month - 1];

    if (Month == 2 && NtfsIsLeapYear(Year)) {
        Days++;
    }

    return Days;
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one common year.
 *
 * @return Number of 100ns intervals in 365 days.
 */
static U64 NtfsTicksPerCommonYear(void) {
    return U64_Make(0x00011ED1, 0x78C6C000);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one leap year.
 *
 * @return Number of 100ns intervals in 366 days.
 */
static U64 NtfsTicksPerLeapYear(void) {
    return U64_Make(0x00011F9A, 0xA3308000);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one day.
 *
 * @return Number of 100ns intervals in one day.
 */
static U64 NtfsTicksPerDay(void) {
    return U64_Make(0x000000C9, 0x2A69C000);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one hour.
 *
 * @return Number of 100ns intervals in one hour.
 */
static U64 NtfsTicksPerHour(void) {
    return U64_Make(0x00000008, 0x61C46800);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one minute.
 *
 * @return Number of 100ns intervals in one minute.
 */
static U64 NtfsTicksPerMinute(void) {
    return U64_Make(0x00000000, 0x23C34600);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one second.
 *
 * @return Number of 100ns intervals in one second.
 */
static U64 NtfsTicksPerSecond(void) {
    return U64_Make(0x00000000, 0x00989680);
}

/***************************************************************************/

/**
 * @brief Return NTFS tick constant for one millisecond.
 *
 * @return Number of 100ns intervals in one millisecond.
 */
static U64 NtfsTicksPerMillisecond(void) {
    return U64_Make(0x00000000, 0x00002710);
}

/***************************************************************************/

/**
 * @brief Convert an NTFS timestamp to DATETIME.
 *
 * NTFS timestamps use 100-nanosecond intervals since January 1st, 1601.
 *
 * @param NtfsTimestamp Timestamp value from NTFS metadata.
 * @param DateTime Destination DATETIME structure.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL NtfsTimestampToDateTime(U64 NtfsTimestamp, LPDATETIME DateTime) {
    U32 Year = 1601;
    U32 Month = 1;
    U32 Day = 1;
    U32 Hour = 0;
    U32 Minute = 0;
    U32 Second = 0;
    U32 Milli = 0;
    U32 DayIndex = 0;
    U64 RemainingTicks;

    if (DateTime == NULL) return FALSE;

    MemorySet(DateTime, 0, sizeof(DATETIME));
    RemainingTicks = NtfsTimestamp;

    while (TRUE) {
        U64 YearTicks = NtfsIsLeapYear(Year) ? NtfsTicksPerLeapYear() : NtfsTicksPerCommonYear();
        if (U64_Cmp(RemainingTicks, YearTicks) < 0) {
            break;
        }
        RemainingTicks = U64_Sub(RemainingTicks, YearTicks);
        Year++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerDay()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerDay());
        DayIndex++;
    }

    Month = 1;
    while (Month <= 12) {
        U32 DaysInMonth = NtfsGetDaysInMonth(Year, Month);
        if (DayIndex < DaysInMonth) {
            Day = DayIndex + 1;
            break;
        }
        DayIndex -= DaysInMonth;
        Month++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerHour()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerHour());
        Hour++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerMinute()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerMinute());
        Minute++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerSecond()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerSecond());
        Second++;
    }

    while (U64_Cmp(RemainingTicks, NtfsTicksPerMillisecond()) >= 0) {
        RemainingTicks = U64_Sub(RemainingTicks, NtfsTicksPerMillisecond());
        Milli++;
    }

    DateTime->Year = Year;
    DateTime->Month = Month;
    DateTime->Day = Day;
    DateTime->Hour = Hour;
    DateTime->Minute = Minute;
    DateTime->Second = Second;
    DateTime->Milli = Milli;

    return TRUE;
}

/***************************************************************************/
