
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Clock.h"

#include "../include/I386.h"
#include "../include/Schedule.h"
#include "../include/String.h"
#include "../include/System.h"
#include "../include/Text.h"

/***************************************************************************/

#define CLOCK_FREQUENCY 1000

/***************************************************************************/

static U32 RawSystemTime = 0;

/***************************************************************************/

void InitializeClock() {
    // The 8254 Timer Chip receives 1,193,180 signals from
    // the system, so to increment a 10 millisecond counter,
    // our interrupt handler must be called every 11,932 signal
    // 1,193,180 / 11,932 = 99.99832384

    U32 Flags;

    SaveFlags(&Flags);
    DisableInterrupts();

    OutPortByte(CLOCK_COMMAND, 0x36);
    OutPortByte(CLOCK_DATA, (U8)(11932 >> 0));
    OutPortByte(CLOCK_DATA, (U8)(11932 >> 8));

    RestoreFlags(&Flags);

    EnableIRQ(0);
}

/***************************************************************************/

U32 GetSystemTime() { return RawSystemTime; }

/***************************************************************************/

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

void ClockHandler() {
    static BOOL Busy = FALSE;

    if (Busy == FALSE) {
        Busy = TRUE;
        RawSystemTime += 10;
        Busy = FALSE;
    }

    Scheduler();
}

/***************************************************************************/

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

BOOL GetLocalTime(LPSYSTEMTIME Time) {
    Time->Year = ReadCMOS(CMOS_YEAR);
    Time->Month = ReadCMOS(CMOS_MONTH);
    Time->Day = ReadCMOS(CMOS_DAY_OF_MONTH);
    Time->Hour = ReadCMOS(CMOS_HOUR);
    Time->Minute = ReadCMOS(CMOS_MINUTE);
    Time->Second = ReadCMOS(CMOS_SECOND);
    Time->Milli = 0;

    return TRUE;
}

/***************************************************************************/
