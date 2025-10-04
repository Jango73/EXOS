
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


    Network Checksum Utilities

\************************************************************************/

#ifndef NETWORKCHECKSUM_H_INCLUDED
#define NETWORKCHECKSUM_H_INCLUDED

#include "Base.h"

/************************************************************************/

U32 NetworkChecksum_Calculate_Accumulate(const U8* Data, U32 Length, U32 Accumulator);
U16 NetworkChecksum_Finalize(U32 Accumulator);
U16 NetworkChecksum_Calculate(const U8* Data, U32 Length);

/************************************************************************/

#endif // NETWORKCHECKSUM_H_INCLUDED
