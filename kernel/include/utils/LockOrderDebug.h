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


    Debug lock order checker

\************************************************************************/

#ifndef LOCK_ORDER_DEBUG_H_INCLUDED
#define LOCK_ORDER_DEBUG_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

void AcquireLockRole(U32 Role, LPCSTR RoleName);
void ReleaseLockRole(U32 Role, LPCSTR RoleName);
void LockOrderDebugAcquire(U32 Role, LPCSTR RoleName);
void LockOrderDebugRelease(U32 Role, LPCSTR RoleName);

/************************************************************************/

#endif  // LOCK_ORDER_DEBUG_H_INCLUDED
