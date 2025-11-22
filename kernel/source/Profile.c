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

#if CONFIG_PROFILE

/************************************************************************/
// Temporary time source: system clock updated at 10 ms resolution.
static inline UINT ProfileGetTicks(void)
{
    return GetSystemTime();
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
}

/************************************************************************/

/**
 * @brief Dump collected profiling data.
 *
 * Implemented in later steps once storage is in place.
 */
void ProfileDump(void)
{
}

/************************************************************************/

#endif
