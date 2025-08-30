
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


    Clock

\************************************************************************/
#include "../include/Clock.h"

#include "../include/I386.h"
#include "../include/Log.h"
#include "../include/String.h"
#include "../include/System.h"
#include "../include/Text.h"

/***************************************************************************/

#define CLOCK_FREQUENCY 1000

/***************************************************************************/

static U32 RawSystemTime = 10;
static U32 ReadCMOS(U32 Address);
static SYSTEMTIME CurrentTime;
static const U8 DaysInMonth[12] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};

static BOOL IsLeapYear(U32 Year) {
    return (Year % 400 == 0) || ((Year % 4 == 0) && (Year % 100 != 0));
}

static void InitializeLocalTime(void) {
    CurrentTime.Year = ReadCMOS(CMOS_YEAR);
    CurrentTime.Month = ReadCMOS(CMOS_MONTH);
    CurrentTime.Day = ReadCMOS(CMOS_DAY_OF_MONTH);
    CurrentTime.Hour = ReadCMOS(CMOS_HOUR);
    CurrentTime.Minute = ReadCMOS(CMOS_MINUTE);
    CurrentTime.Second = ReadCMOS(CMOS_SECOND);
    CurrentTime.Milli = 0;
}

/***************************************************************************/

/**
 * @brief Initialize the system clock and enable timer interrupts.
 */
void InitializeClock(void) {
    // The 8254 Timer Chip receives 1,193,180 signals from
    // the system, so to increment a 10 millisecond counter,
    // our interrupt handler must be called every 11,932 signal
    // 1,193,180 / 11,932 = 99.99832384

    U32 Flags;

    SaveFlags(&Flags);

    OutPortByte(CLOCK_COMMAND, 0x36);
    OutPortByte(CLOCK_DATA, (U8)(0x2E9C >> 0));
    OutPortByte(CLOCK_DATA, (U8)(0x2E9C >> 8));

    RestoreFlags(&Flags);

    EnableIRQ(0);
    InitializeLocalTime();
}

/***************************************************************************/

/**
 * @brief Retrieve the current system time in milliseconds.
 * @return Number of milliseconds since startup.
 */
U32 GetSystemTime(void) { return RawSystemTime; }

/***************************************************************************/

/**
 * @brief Convert milliseconds to HH:MM:SS text representation.
 * @param MilliSeconds Time in milliseconds.
 * @param Text Destination buffer for formatted string.
 */
void MilliSecondsToHMS(U32 MilliSeconds, LPSTR Text) {
    U32 Seconds = MilliSeconds / 1000;
    U32 H = (Seconds / 3600);
    U32 M = (Seconds / 60) % 60;
    U32 S = (Seconds % 60);
    STR Temp[16];

    // sprintf(Text, "%02u:%02u:%02u", h, m, s);

    Text[0] = STR_NULL;

    if (H < 10) StringConcat(Text, Text_0);
    U32ToString(H, Temp);
    StringConcat(Text, Temp);
    StringConcat(Text, Text_Colon);
    if (M < 10) StringConcat(Text, Text_0);
    U32ToString(M, Temp);
    StringConcat(Text, Temp);
    StringConcat(Text, Text_Colon);
    if (S < 10) StringConcat(Text, Text_0);
    U32ToString(S, Temp);
    StringConcat(Text, Temp);
}

/***************************************************************************/

/**
 * @brief Increment the internal millisecond counter.
 */
void ClockHandler(void) {
    // KernelLogText(LOG_DEBUG, TEXT("[ClockHandler]"));
    RawSystemTime += 10;
    CurrentTime.Milli += 10;
    if (CurrentTime.Milli >= 1000) {
        CurrentTime.Milli -= 1000;
        CurrentTime.Second++;
    }
    if (CurrentTime.Second >= 60) {
        CurrentTime.Second = 0;
        CurrentTime.Minute++;
    }
    if (CurrentTime.Minute >= 60) {
        CurrentTime.Minute = 0;
        CurrentTime.Hour++;
    }
    if (CurrentTime.Hour >= 24) {
        CurrentTime.Hour = 0;
        CurrentTime.Day++;
        U32 DaysInCurrentMonth = DaysInMonth[CurrentTime.Month - 1];
        if (CurrentTime.Month == 2 && IsLeapYear(CurrentTime.Year)) {
            DaysInCurrentMonth++;
        }
        if (CurrentTime.Day > DaysInCurrentMonth) {
            CurrentTime.Day = 1;
            CurrentTime.Month++;
            if (CurrentTime.Month > 12) {
                CurrentTime.Month = 1;
                CurrentTime.Year++;
            }
        }
    }
}

/***************************************************************************/

/**
 * @brief Read a byte from the CMOS at the given address.
 * @param Address CMOS register address.
 * @return Value read from CMOS.
 */
static U32 ReadCMOS(U32 Address) {
    OutPortByte(CMOS_COMMAND, Address);
    return InPortByte(CMOS_DATA);
}

/***************************************************************************/

/*
static void WriteCMOS(U32 Address, U32 Value) {
    OutPortByte(CMOS_COMMAND, Address);
    OutPortByte(CMOS_DATA, Value);
}
*/

/***************************************************************************/

/**
 * @brief Retrieve current time from CMOS into a SYSTEMTIME structure.
 * @param Time Destination structure for the current time.
 * @return TRUE on success.
 */
BOOL GetLocalTime(LPSYSTEMTIME Time) {
    if (Time == NULL) return FALSE;
    *Time = CurrentTime;
    return TRUE;
}

BOOL SetLocalTime(LPSYSTEMTIME Time) {
    if (Time == NULL) return FALSE;
    CurrentTime = *Time;
    return TRUE;
}

/***************************************************************************/

