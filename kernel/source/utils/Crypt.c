
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


    Crypt

\************************************************************************/

#include "utils/Crypt.h"

#include "CoreString.h"
#include "System.h"

/***************************************************************************/

/* Reflected polynomial for CRC-32 (IEEE 802.3) */
#define CRC32_POLY ((U32)0xEDB88320u)
#define CRC32_INIT ((U32)0xFFFFFFFFu)
#define CRC32_FINAL_XOR ((U32)0xFFFFFFFFu)

/***************************************************************************/

/**
 * @brief Process one byte for the CRC32 computation.
 * @param Crc Current CRC value.
 * @param Byte Input byte.
 * @return Updated CRC value.
 */
static inline U32 CRC32_ProcessByte(U32 Crc, U8 Byte) {
    U32 c = Crc ^ (U32)Byte;
    for (UINT i = 0; i < 8; ++i) {
        U32 mask = (c & 1u) ? CRC32_POLY : 0u;
        c = (c >> 1) ^ mask;
    }
    return c;
}

/***************************************************************************/

/**
 * @brief Initialize a CRC32 context for streaming operations.
 * @param Ctx CRC32 context to initialize.
 */
void CRC32Begin(LPCRC32_CTX Ctx) {
    /* State holds the "internal" CRC value (before final XOR). */
    Ctx->State = CRC32_INIT;
}

/**
 * @brief Update a CRC32 context with new data.
 * @param Ctx CRC32 context.
 * @param Data Pointer to data block.
 * @param Length Number of bytes to process.
 */
void CRC32Update(LPCRC32_CTX Ctx, const void *Data, U32 Length) {
    const U8 *p = (const U8 *)Data;
    U32 c = Ctx->State;

    while (Length--) {
        c = CRC32_ProcessByte(c, *p++);
    }

    Ctx->State = c;
}
/**
 * @brief Finalize the CRC32 computation and return the checksum.
 * @param Ctx CRC32 context.
 * @return Final CRC32 value.
 */
U32 CRC32Final(LPCRC32_CTX Ctx) {
    /* Apply final XOR and return; also store finalized value in context. */
    Ctx->State ^= CRC32_FINAL_XOR;
    return Ctx->State;
}

/***************************************************************************/

/**
 * @brief Compute a CRC32 checksum in a single call.
 * @param Data Pointer to data block.
 * @param Length Number of bytes to process.
 * @return CRC32 checksum of the data.
 */
U32 CRC32(const void *Data, U32 Length) {
    CRC32_CTX ctx;
    CRC32Begin(&ctx);
    CRC32Update(&ctx, Data, Length);
    return CRC32Final(&ctx);
}
