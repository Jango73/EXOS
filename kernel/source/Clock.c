
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


    Clock manager

\************************************************************************/

#include "../include/Clock.h"

#include "../include/arch/i386/I386.h"
#include "../include/InterruptController.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Schedule.h"
#include "../include/String.h"
#include "../include/System.h"
#include "../include/Text.h"

/************************************************************************/
// Timer resolution

#define DIVISOR 11932
#define MILLIS 10
#define SCHEDULING_PERIOD_MILLIS 10

/************************************************************************/

static U32 SystemUpTime = 0;
static U32 SchedulerTime = 0;
static DATETIME CurrentTime;
static const U8 DaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

#if SCHEDULING_DEBUG_OUTPUT == 1
static logCount = 0;
#endif

/************************************************************************/

static BOOL IsLeapYear(U32 Year) { return (Year % 400 == 0) || ((Year % 4 == 0) && (Year % 100 != 0)); }

/************************************************************************/

static U32 BCDToInteger(U32 Value) { return ((Value >> 4) * 10) + (Value & 0x0F); }

/************************************************************************/

/**
 * @brief Read a byte from the CMOS at the given address.
 * @param Address CMOS register address.
 * @return Value read from CMOS.
 */
static U32 ReadCMOS(U32 Address) {
    OutPortByte(CMOS_COMMAND, Address);
    return InPortByte(CMOS_DATA);
}

/************************************************************************/

static void InitializeLocalTime(void) {
    CurrentTime.Year = 2000 + BCDToInteger(ReadCMOS(CMOS_YEAR));
    CurrentTime.Month = BCDToInteger(ReadCMOS(CMOS_MONTH));
    CurrentTime.Day = BCDToInteger(ReadCMOS(CMOS_DAY_OF_MONTH));
    CurrentTime.Hour = BCDToInteger(ReadCMOS(CMOS_HOUR));
    CurrentTime.Minute = BCDToInteger(ReadCMOS(CMOS_MINUTE));
    CurrentTime.Second = BCDToInteger(ReadCMOS(CMOS_SECOND));
    CurrentTime.Milli = 0;
}

/************************************************************************/

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
    OutPortByte(CLOCK_DATA, (U8)(DIVISOR >> 0));
    OutPortByte(CLOCK_DATA, (U8)(DIVISOR >> 8));

    RestoreFlags(&Flags);

    EnableInterrupt(0);
    InitializeLocalTime();
}

/************************************************************************/

void ManageLocalTime(void) {
    CurrentTime.Milli -= 1000;
    CurrentTime.Second++;

    if (CurrentTime.Second >= 60) {
        CurrentTime.Second = 0;
        CurrentTime.Minute++;

        if (CurrentTime.Minute >= 60) {
            CurrentTime.Minute = 0;
            CurrentTime.Hour++;

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
    }
}

/************************************************************************/

/**
 * @brief Increment the internal millisecond counter.
 */
void ClockHandler(void) {
#if SCHEDULING_DEBUG_OUTPUT == 1
    logCount++;
    if (logCount > 20000) {
        DEBUG(TEXT("Too much flooding, halting system."));
        DO_THE_SLEEPING_BEAUTY;
    }
#endif

    SystemUpTime += MILLIS;
    SchedulerTime += MILLIS;
    CurrentTime.Milli += MILLIS;

    if (CurrentTime.Milli >= 1000) {
        ManageLocalTime();
    }

    if (SchedulerTime >= (Kernel.MinimumQuantum + SCHEDULING_PERIOD_MILLIS)) {
        SchedulerTime = 0;
        Scheduler();
    }

    /*
    if (SystemUpTime % 1000 == 0) {
        DEBUG(TEXT("[ClockHandler] Time = %d"), SystemUpTime);
    }
    */
}

/************************************************************************/

/**
 * @brief Retrieve the current system time in milliseconds.
 * @return Number of milliseconds since startup.
 */
U32 GetSystemTime(void) { return SystemUpTime; }

/************************************************************************/

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

/************************************************************************/

/*
static void WriteCMOS(U32 Address, U32 Value) {
    OutPortByte(CMOS_COMMAND, Address);
    OutPortByte(CMOS_DATA, Value);
}
*/

/************************************************************************/

/**
 * @brief Retrieve current time from CMOS into a DATETIME structure.
 * @param Time Destination structure for the current time.
 * @return TRUE on success.
 */
BOOL GetLocalTime(LPDATETIME Time) {
    if (Time == NULL) return FALSE;
    if (CurrentTime.Year == 0) {
        InitializeLocalTime();
    }
    *Time = CurrentTime;
    return TRUE;
}

/************************************************************************/

BOOL SetLocalTime(LPDATETIME Time) {
    if (Time == NULL) return FALSE;
    CurrentTime = *Time;
    return TRUE;
}

/************************************************************************/

void RTCHandler(void) { DEBUG(TEXT("[RTCHandler]")); }

/************************************************************************/

void PIC2Handler(void) { DEBUG(TEXT("[PIC2Handler]")); }

/************************************************************************/

void FPUHandler(void) { DEBUG(TEXT("[FPUHandler]")); }
