
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


    CRC64-ECMA

\************************************************************************/

#include "../include/Crypt.h"
#include "../include/String.h"

/************************************************************************/

// CRC64 implementation for EXOS
// CRC64-ECMA polynomial (used by Redis, PostgreSQL...)

static const U64 CRC64_Poly = {0xD7870F42, 0xC96C5795};  // 0xC96C5795D7870F42ULL
static U64 CRC64_Table[256];
static BOOL CRC64_TableInitialized = FALSE;

/************************************************************************/

// Helper functions for U64 operations in CRC64
inline U64 U64_ShiftRight1(U64 Value) {
    U64 Result;
    Result.LO = (Value.LO >> 1) | ((Value.HI & 1) << 31);
    Result.HI = Value.HI >> 1;
    return Result;
}

inline U64 U64_Xor(U64 A, U64 B) {
    U64 Result;
    Result.LO = A.LO ^ B.LO;
    Result.HI = A.HI ^ B.HI;
    return Result;
}

inline BOOL U64_IsOdd(U64 Value) {
    return (Value.LO & 1) != 0;
}

inline U64 U64_FromU32(U32 Value) {
    U64 Result;
    Result.LO = Value;
    Result.HI = 0;
    return Result;
}

inline U64 U64_ShiftRight8(U64 Value) {
    U64 Result;
    Result.LO = (Value.LO >> 8) | ((Value.HI & 0xFF) << 24);
    Result.HI = Value.HI >> 8;
    return Result;
}

/************************************************************************/

void CRC64_InitTable(void) {
    if (CRC64_TableInitialized) return;
    
    for (int TableIndex = 0; TableIndex < 256; TableIndex++) {
        U64 Crc = U64_FromU32(TableIndex);
        for (int BitIndex = 0; BitIndex < 8; BitIndex++) {
            if (U64_IsOdd(Crc)) {
                Crc = U64_Xor(U64_ShiftRight1(Crc), CRC64_Poly);
            } else {
                Crc = U64_ShiftRight1(Crc);
            }
        }
        CRC64_Table[TableIndex] = Crc;
    }
    CRC64_TableInitialized = TRUE;
}

/************************************************************************/

U64 CRC64_Hash(const void* Data, U32 Length) {
    if (!CRC64_TableInitialized) {
        CRC64_InitTable();
    }
    
    U64 Crc = {0xFFFFFFFF, 0xFFFFFFFF};  // Initial value: all ones
    const U8* Bytes = (const U8*)Data;
    
    for (U32 ByteIndex = 0; ByteIndex < Length; ByteIndex++) {
        U8 TableIndex = (U8)(Crc.LO ^ Bytes[ByteIndex]);
        Crc = U64_Xor(U64_ShiftRight8(Crc), CRC64_Table[TableIndex]);
    }
    
    // Final XOR with all ones
    U64 FinalMask = {0xFFFFFFFF, 0xFFFFFFFF};
    return U64_Xor(Crc, FinalMask);
}

/************************************************************************/

// Helper to hash a username
U64 HashString(const LPCSTR Text) {
    return CRC64_Hash(Text, StringLength(Text));
}
