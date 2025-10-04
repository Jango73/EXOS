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


    UUID helpers

\************************************************************************/

#include "../include/UUID.h"

/***************************************************************************/

/**
 * @brief Generate a pseudo-random 32-bit value.
 *
 * This stub implementation uses a Xorshift32 PRNG. Replace the entropy
 * source with a hardware-backed generator when available.
 *
 * @return Pseudo-random 32-bit value.
 */
static U32 OS_Rand32(void)
{
    static U32 Seed = 0xC0FFEE12;

    Seed ^= Seed << 13;
    Seed ^= Seed >> 17;
    Seed ^= Seed << 5;
    return Seed;
}

/***************************************************************************/

/**
 * @brief Generate an RFC 4122 compliant binary UUID (version 4).
 *
 * @param Out Pointer to a buffer that receives 16 bytes of UUID data.
 */
void UUID_Generate(U8* Out)
{
    U32 Value;
    U32 Index;
    U32 Offset;

    if (Out == NULL) {
        return;
    }

    for (Index = 0; Index < UUID_BINARY_SIZE; Index += sizeof(U32)) {
        Value = OS_Rand32();
        for (Offset = 0; Offset < sizeof(U32); ++Offset) {
            Out[Index + Offset] = (U8)((Value >> ((3U - Offset) * 8U)) & 0xFFU);
        }
    }

    Out[6] = (Out[6] & 0x0FU) | 0x40U;
    Out[8] = (Out[8] & 0x3FU) | 0x80U;
}

/***************************************************************************/

U64 UUID_ToU64(const U8* uuid)
{
    U64 result;
    U32 i;

    result.LO = 0;
    result.HI = 0;

    // Accumule les 8 premiers octets
    for (i = 0; i < 8; ++i)
        result.LO = (result.LO << 8) | uuid[i];

    // XOR avec les 8 derniers
    for (i = 8; i < 16; ++i)
        result.HI = (result.HI << 8) | uuid[i];

    result.LO ^= result.HI;
    result.HI = 0; // optionnel, selon ta convention

    return result;
}

/***************************************************************************/

/**
 * @brief Convert a binary UUID into its textual representation.
 *
 * @param In Pointer to the 16-byte binary UUID.
 * @param Out Pointer to a buffer that receives the null-terminated string.
 */
void UUID_ToString(const U8* In, char* Out)
{
    static const char HEX[16] = {
        '0','1','2','3','4','5','6','7',
        '8','9','a','b','c','d','e','f'
    };

    U32 Index;
    U32 Position = 0;

    if (In == NULL || Out == NULL) {
        return;
    }

    for (Index = 0; Index < UUID_BINARY_SIZE; ++Index) {
        if (Index == 4 || Index == 6 || Index == 8 || Index == 10) {
            Out[Position++] = '-';
        }

        Out[Position++] = HEX[(In[Index] >> 4) & 0x0FU];
        Out[Position++] = HEX[In[Index] & 0x0FU];
    }

    Out[Position] = '\0';
}

/***************************************************************************/
