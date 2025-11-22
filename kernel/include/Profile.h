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

#ifndef PROFILE_H_INCLUDED
#define PROFILE_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#define PROFILE_SCOPE_STATE_INACTIVE 0
#define PROFILE_SCOPE_STATE_ACTIVE   1

/************************************************************************/

typedef struct tagPROFILE_SCOPE {
    LPCSTR Name;
    UINT StartTicks;
    UINT State;
} PROFILE_SCOPE, *LPPROFILE_SCOPE;

/************************************************************************/

#if CONFIG_PROFILE

void ProfileStart(LPPROFILE_SCOPE Scope, LPCSTR Name);
void ProfileStop(LPPROFILE_SCOPE Scope);
void ProfileDump(void);

#else

static inline void ProfileStart(LPPROFILE_SCOPE Scope, LPCSTR Name)
{
    UNUSED(Scope);
    UNUSED(Name);
}

static inline void ProfileStop(LPPROFILE_SCOPE Scope)
{
    UNUSED(Scope);
}

static inline void ProfileDump(void)
{
}

#endif

/************************************************************************/

static inline void ProfileScopeBegin(LPPROFILE_SCOPE Scope, LPCSTR Name)
{
    ProfileStart(Scope, Name);
}

/************************************************************************/

static inline void ProfileScopeEnd(LPPROFILE_SCOPE Scope)
{
    ProfileStop(Scope);
}

/************************************************************************/

#define PROFILE_SCOPED(name_literal)                                                   \
    for (PROFILE_SCOPE _ProfileScope = {0}, *_ProfileScopePtr = &_ProfileScope;        \
         _ProfileScopePtr != NULL;                                                     \
         ProfileScopeEnd(_ProfileScopePtr), _ProfileScopePtr = NULL)                   \
        for (ProfileScopeBegin(_ProfileScopePtr, TEXT(name_literal));                  \
             _ProfileScopePtr != NULL;                                                 \
             _ProfileScopePtr = NULL)

/************************************************************************/

#endif // PROFILE_H_INCLUDED
