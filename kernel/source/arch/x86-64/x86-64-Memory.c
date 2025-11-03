
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


    x86-64-specific memory helpers

\************************************************************************/

#include "Memory.h"

#include "Console.h"
#include "CoreString.h"
#include "Kernel.h"
#include "Log.h"
#include "Stack.h"
#include "System.h"
#include "Text.h"
#include "arch/x86-64/x86-64.h"
#include "arch/x86-64/x86-64-Log.h"

/************************************************************************/

void ArchRemapTemporaryPage(LINEAR Linear, PHYSICAL Physical) {
    UNUSED(Linear);
    UNUSED(Physical);
    ConsolePanic(TEXT("[ArchRemapTemporaryPage] Not implemented for x86-64"));
}

LINEAR ArchAllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags) {
    UNUSED(Base);
    UNUSED(Target);
    UNUSED(Size);
    UNUSED(Flags);
    ConsolePanic(TEXT("[ArchAllocRegion] Not implemented for x86-64"));
    return NULL;
}

BOOL ArchResizeRegion(LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags) {
    UNUSED(Base);
    UNUSED(Target);
    UNUSED(Size);
    UNUSED(NewSize);
    UNUSED(Flags);
    ConsolePanic(TEXT("[ArchResizeRegion] Not implemented for x86-64"));
    return FALSE;
}

BOOL ArchFreeRegion(LINEAR Base, UINT Size) {
    UNUSED(Base);
    UNUSED(Size);
    ConsolePanic(TEXT("[ArchFreeRegion] Not implemented for x86-64"));
    return FALSE;
}

/************************************************************************/
