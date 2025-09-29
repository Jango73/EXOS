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


    Stack operations

\************************************************************************/

#ifndef STACK_H_INCLUDED
#define STACK_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

// Copy stack content from source to destination and adjust frame pointers
BOOL CopyStack(LINEAR DestStackTop, LINEAR SourceStackTop, U32 Size);

// Copy stack content with specified EBP instead of using GetEBP()
BOOL CopyStackWithEBP(LINEAR DestStackTop, LINEAR SourceStackTop, U32 Size, LINEAR StartEBP);

// Copy stack and switch ESP/EBP to new location
BOOL SwitchStack(LINEAR DestStackTop, LINEAR SourceStackTop, U32 Size);

// Check current task's stack safety
BOOL CheckStack(void);

#endif
