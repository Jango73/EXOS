
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


    Profiling helpers

\************************************************************************/

#include "Profile.h"
#include "Clock.h"
#include "utils/CircularBuffer.h"
#include "CoreString.h"
#include "Log.h"
#include "Console.h"
#include "System.h"

#if PROFILING

/************************************************************************/
// Time source: PIT counter read (channel 0, latched), for sub-ms deltas.
// We combine PIT ticks with coarse GetSystemTime() to handle wrap.
static inline UINT ProfileGetTicks(void)
{
    U32 Flags;
    U8 Low;
    U8 High;

    SaveFlags(&Flags);
    DisableInterrupts();

    OutPortByte(0x43, 0x00);           // Latch channel 0 count
    Low = InPortByte(0x40);            // Read low byte
    High = InPortByte(0x40);           // Read high byte

    RestoreFlags(&Flags);

    UINT Latched = ((UINT)High << 8) | (UINT)Low;

    // Combine with coarse system time to avoid wrap confusion
    // (PIT counts down from DIVISOR to 0).
    UINT CoarseMs = GetSystemTime();
    return (CoarseMs << 16) | Latched;
}

/************************************************************************/

#define PROFILE_MAX_STATS 64
#define PIT_FREQUENCY 1193180u
#define PIT_DIVISOR 11932u

static PROFILE_STATS ProfileStats[PROFILE_MAX_STATS];
static UINT ProfileStatsCount = 0;
static UINT ProfileSamplesWritten = 0;
static UINT ProfileSamplesDropped = 0;
static UINT ProfileSamplesLogged = 0;

/************************************************************************/

static UINT ProfileReadPITCount(void)
{
    U32 Flags;
    U8 Low;
    U8 High;

    SaveFlags(&Flags);
    DisableInterrupts();

    OutPortByte(0x43, 0x00);           // Latch channel 0 count
    Low = InPortByte(0x40);            // Read low byte
    High = InPortByte(0x40);           // Read high byte

    RestoreFlags(&Flags);

    return ((UINT)High << 8) | (UINT)Low;
}

/************************************************************************/

static LPPROFILE_STATS ProfileFindOrCreate(LPCSTR Name)
{
    for (UINT Index = 0; Index < ProfileStatsCount; ++Index)
    {
        if (ProfileStats[Index].Name == Name)
        {
            return &ProfileStats[Index];
        }
    }

    if (ProfileStatsCount >= PROFILE_MAX_STATS)
    {
        ProfileSamplesDropped++;
        return NULL;
    }

    ProfileStats[ProfileStatsCount].Name = Name;
    ProfileStats[ProfileStatsCount].Count = 0;
    ProfileStats[ProfileStatsCount].LastTicks = 0;
    ProfileStats[ProfileStatsCount].TotalTicks = 0;
    ProfileStats[ProfileStatsCount].MaxTicks = 0;

    ProfileStatsCount++;
    return &ProfileStats[ProfileStatsCount - 1];
}

/************************************************************************/

static void ProfileRecordSample(LPCSTR Name, UINT DurationTicks)
{
    if (Name == NULL)
    {
        return;
    }

    LPPROFILE_STATS Entry = ProfileFindOrCreate(Name);
    if (Entry == NULL)
    {
        ProfileSamplesDropped++;
        return;
    }

    Entry->Count++;
    Entry->LastTicks = DurationTicks;
    Entry->TotalTicks += DurationTicks;
    if (DurationTicks > Entry->MaxTicks)
    {
        Entry->MaxTicks = DurationTicks;
    }

    ProfileSamplesWritten++;

    if (ProfileSamplesLogged < 8)
    {
        DEBUG(TEXT("[ProfileRecordSample] name=%s duration=%u ticks=%u written=%u dropped=%u"),
              Entry->Name,
              DurationTicks,
              DurationTicks,
              ProfileSamplesWritten,
              ProfileSamplesDropped);
        ProfileSamplesLogged++;
    }
}

/************************************************************************/

/**
 * @brief Start a profiling scope.
 *
 * Initializes the scope with the provided name and records the start tick
 * placeholder. Timing is refined in later steps.
 *
 * @param Scope Profiling scope structure to initialize.
 * @param Name  Scope name (literal or static string).
 */
void ProfileStart(LPPROFILE_SCOPE Scope, LPCSTR Name)
{
    if (Scope == NULL)
    {
        return;
    }

    Scope->Name = Name;
    Scope->StartMillis = GetSystemTime();
    Scope->StartCount = ProfileReadPITCount();
    Scope->State = PROFILE_SCOPE_STATE_ACTIVE;
}

/************************************************************************/

/**
 * @brief Stop a profiling scope.
 *
 * Records completion of the scope. Duration accounting is added in later
 * steps alongside storage and timing sources.
 *
 * @param Scope Profiling scope to finalize.
 */
void ProfileStop(LPPROFILE_SCOPE Scope)
{
    if (Scope == NULL)
    {
        return;
    }

    Scope->State = PROFILE_SCOPE_STATE_INACTIVE;

    UINT EndMillis = GetSystemTime();
    UINT EndCount = ProfileReadPITCount();

    UINT BaseMillis = (EndMillis >= Scope->StartMillis) ? (EndMillis - Scope->StartMillis) : 0;
    UINT BaseMicros = BaseMillis * 1000;

    UINT StartOffsetMicros = ((PIT_DIVISOR - Scope->StartCount) * 1000000u) / PIT_FREQUENCY;
    UINT EndOffsetMicros = ((PIT_DIVISOR - EndCount) * 1000000u) / PIT_FREQUENCY;

    INT OffsetDelta = (INT)EndOffsetMicros - (INT)StartOffsetMicros;
    INT DurationMicrosSigned = (INT)BaseMicros + OffsetDelta;

    UINT DurationMicros = (DurationMicrosSigned >= 0) ? (UINT)DurationMicrosSigned : 0;

    ProfileRecordSample(Scope->Name, DurationMicros);
}

/************************************************************************/

/**
 * @brief Dump collected profiling data.
 *
 * Implemented in later steps once storage is in place.
 */
void ProfileDump(void)
{
    if (ProfileStatsCount == 0)
    {
        DEBUG(TEXT("[ProfileDump] No samples available (written=%u dropped=%u)"),
              ProfileSamplesWritten,
              ProfileSamplesDropped);
        return;
    }

    for (UINT StatIndex = 0; StatIndex < ProfileStatsCount; ++StatIndex)
    {
        UINT Average = (ProfileStats[StatIndex].Count > 0)
                           ? (ProfileStats[StatIndex].TotalTicks / ProfileStats[StatIndex].Count)
                           : 0;

        DEBUG(TEXT("[ProfileDump] name=%s count=%u last=%u us avg=%u us max=%u us total=%u us"),
              ProfileStats[StatIndex].Name,
              ProfileStats[StatIndex].Count,
              ProfileStats[StatIndex].LastTicks,
              Average,
              ProfileStats[StatIndex].MaxTicks,
              ProfileStats[StatIndex].TotalTicks);
    }

    DEBUG(TEXT("[ProfileDump] samples=%u dropped=%u"),
          ProfileSamplesWritten,
          ProfileSamplesDropped);
}

/************************************************************************/
#else

void ProfileStart(LPPROFILE_SCOPE Scope, LPCSTR Name)
{
    UNUSED(Scope);
    UNUSED(Name);
}

/************************************************************************/

void ProfileStop(LPPROFILE_SCOPE Scope)
{
    UNUSED(Scope);
}

/************************************************************************/

void ProfileDump(void)
{
    DEBUG(TEXT("[ProfileDump] Profiling disabled"));
}

/************************************************************************/

#endif
