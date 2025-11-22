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


    Cooldown helper

\************************************************************************/

#ifndef COOLDOWN_H_INCLUDED
#define COOLDOWN_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_COOLDOWN {
    U32 IntervalMS;
    U32 NextAllowedTick;
    BOOL Initialized;
} COOLDOWN, *LPCOOLDOWN;

/************************************************************************/

BOOL CooldownInit(LPCOOLDOWN Cooldown, U32 IntervalMS);
void CooldownSetInterval(LPCOOLDOWN Cooldown, U32 IntervalMS);
BOOL CooldownTryArm(LPCOOLDOWN Cooldown, U32 Now);
BOOL CooldownReady(LPCOOLDOWN Cooldown, U32 Now);
U32 CooldownRemaining(LPCOOLDOWN Cooldown, U32 Now);

/************************************************************************/

#pragma pack(pop)

#endif
