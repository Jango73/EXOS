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


    UUID generation helpers

\************************************************************************/

#ifndef UUID_H_INCLUDED
#define UUID_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#define UUID_BINARY_SIZE 16
#define UUID_STRING_SIZE 37

/***************************************************************************/

void UUID_Generate(U8* Out);
U64 UUID_ToU64(const U8* uuid);
void UUID_ToString(const U8* In, char* Out);

/***************************************************************************/

#endif  // UUID_H_INCLUDED
