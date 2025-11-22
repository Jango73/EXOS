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

#if CONFIG_PROFILE

/************************************************************************/
// Temporary time source: system clock updated at 10 ms resolution.
static inline UINT ProfileGetTicks(void)
{
    return GetSystemTime();
}

/************************************************************************/

#define PROFILE_BUFFER_ENTRIES 512

static CIRCULAR_BUFFER ProfileBuffer;
static PROFILE_SAMPLE ProfileStorage[PROFILE_BUFFER_ENTRIES];
static BOOL ProfileBufferInitialized = FALSE;

/************************************************************************/

static void ProfileEnsureBuffer(void)
{
    if (ProfileBufferInitialized == FALSE)
    {
        CircularBuffer_Initialize(&ProfileBuffer,
                                  (U8*)ProfileStorage,
                                  sizeof(ProfileStorage),
                                  sizeof(ProfileStorage));
        ProfileBufferInitialized = TRUE;
    }
}

/************************************************************************/

static void ProfileRecordSample(LPCSTR Name, UINT DurationTicks)
{
    if (Name == NULL)
    {
        return;
    }

    ProfileEnsureBuffer();

    PROFILE_SAMPLE Sample;
    Sample.Name = Name;
    Sample.DurationTicks = DurationTicks;

    U32 Written = CircularBuffer_Write(&ProfileBuffer, (U8*)&Sample, sizeof(Sample));
    if (Written != sizeof(Sample))
    {
        WARNING(TEXT("[ProfileRecordSample] Buffer overflow (written=%u size=%u)"),
                Written,
                (UINT)sizeof(Sample));
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
    Scope->StartTicks = ProfileGetTicks();
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

    UINT EndTicks = ProfileGetTicks();
    UINT DurationTicks = (EndTicks >= Scope->StartTicks) ? (EndTicks - Scope->StartTicks) : 0;
    ProfileRecordSample(Scope->Name, DurationTicks);
}

/************************************************************************/

/**
 * @brief Dump collected profiling data.
 *
 * Implemented in later steps once storage is in place.
 */
void ProfileDump(void)
{
    ProfileEnsureBuffer();

    U32 AvailableBytes = CircularBuffer_GetAvailableData(&ProfileBuffer);
    UINT SampleCount = (UINT)(AvailableBytes / sizeof(PROFILE_SAMPLE));

    if (SampleCount == 0)
    {
        VERBOSE(TEXT("[ProfileDump] No samples available"));
        return;
    }

    PROFILE_STATS Stats[64];
    UINT StatsCount = 0;

    for (UINT Index = 0; Index < SampleCount; ++Index)
    {
        PROFILE_SAMPLE Sample;
        U32 Read = CircularBuffer_Read(&ProfileBuffer, (U8*)&Sample, sizeof(Sample));
        if (Read != sizeof(Sample))
        {
            WARNING(TEXT("[ProfileDump] Partial read (read=%u expected=%u)"),
                    Read,
                    (UINT)sizeof(Sample));
            break;
        }

        UINT StatIndex = MAX_U32;
        for (UINT StatScan = 0; StatScan < StatsCount; ++StatScan)
        {
            if (Stats[StatScan].Name == Sample.Name)
            {
                StatIndex = StatScan;
                break;
            }
        }

        if (StatIndex == MAX_U32)
        {
            if (StatsCount >= (UINT)(sizeof(Stats) / sizeof(Stats[0])))
            {
                WARNING(TEXT("[ProfileDump] Stats table full (limit=%u)"), StatsCount);
                continue;
            }

            StatIndex = StatsCount++;
            Stats[StatIndex].Name = Sample.Name;
            Stats[StatIndex].Count = 0;
            Stats[StatIndex].LastTicks = 0;
            Stats[StatIndex].TotalTicks = 0;
            Stats[StatIndex].MaxTicks = 0;
        }

        Stats[StatIndex].Count++;
        Stats[StatIndex].LastTicks = Sample.DurationTicks;
        Stats[StatIndex].TotalTicks += Sample.DurationTicks;
        if (Sample.DurationTicks > Stats[StatIndex].MaxTicks)
        {
            Stats[StatIndex].MaxTicks = Sample.DurationTicks;
        }
    }

    for (UINT StatIndex = 0; StatIndex < StatsCount; ++StatIndex)
    {
        UINT Average = (Stats[StatIndex].Count > 0)
                           ? (Stats[StatIndex].TotalTicks / Stats[StatIndex].Count)
                           : 0;

        DEBUG(TEXT("[ProfileDump] name=%s count=%u last=%u ms avg=%u ms max=%u ms total=%u ms"),
              Stats[StatIndex].Name,
              Stats[StatIndex].Count,
              Stats[StatIndex].LastTicks,
              Average,
              Stats[StatIndex].MaxTicks,
              Stats[StatIndex].TotalTicks);
    }
}

/************************************************************************/

#endif
