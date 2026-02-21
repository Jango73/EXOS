
/************************************************************************\

    EXOS UEFI Bootloader
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


    Minimal C runtime compatibility helpers for UEFI builds

\************************************************************************/

#include "Base.h"
#include "vbr-realmode-utils.h"

/************************************************************************/

void* memcpy(void* Destination, const void* Source, UINT Size) {
    MemoryCopy(Destination, Source, Size);
    return Destination;
}

/************************************************************************/

void* memset(void* Destination, int Value, UINT Size) {
    MemorySet(Destination, (UINT)Value, Size);
    return Destination;
}

/************************************************************************/
