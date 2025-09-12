
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
#include "../include/Crypt.h"

#include "../include/String.h"
#include "../include/System.h"

/***************************************************************************/

/* Reflected polynomial for CRC-32 (IEEE 802.3) */
#define CRC32_POLY ((U32)0xEDB88320u)
#define CRC32_INIT ((U32)0xFFFFFFFFu)
#define CRC32_FINAL_XOR ((U32)0xFFFFFFFFu)

/***************************************************************************/

/**
 * @brief Encrypt a password string using a simple hash-based algorithm.
 * @param lpszPassword Source password.
 * @param lpszCrypted Buffer to receive encrypted password.
 * @return TRUE on success.
 */
BOOL MakePassword(LPCSTR lpszPassword, LPSTR lpszCrypted) {
    STR szPass[64];
    STR szEncrypt[64];
    STR szTemp[64];
    U32 PassLength;
    U32 Hash, CurrentBit, c;

    lpszCrypted[0] = STR_NULL;

    StringCopy(szPass, lpszPassword);

    //-------------------------------------
    // Make the password uppercase

    StringToUpper(szPass);

    //-------------------------------------
    // Check if length of password does not exceed 32 characters

    PassLength = StringLength(szPass);

    if (PassLength > 32) return FALSE;

    //-------------------------------------
    // Compute the hashcode of the given password

    Hash = 0;
    for (c = 0; c < PassLength; c++) Hash = (Hash << 1) + szPass[c];

    //-------------------------------------
    // Encrypt the password using the hashcode
    // If the bit in the hashcode corresponding to a character is set,
    // XOR the character with 0xAA else XOR it with 0x55

    for (c = 0; c < PassLength; c++) {
        CurrentBit = (Hash & ((U32)1 << c)) >> c;

        switch (CurrentBit) {
            case 0:
                szEncrypt[c] = (szPass[c] ^ 0x55);
                break;
            case 1:
                szEncrypt[c] = (szPass[c] ^ 0xAA);
                break;
        }
    }

    szEncrypt[c] = STR_NULL;

    //-------------------------------------
    // Reverse the string

    for (c = 0; c < PassLength; c++) {
        szTemp[(PassLength - 1) - c] = szEncrypt[c];
    }

    szTemp[c] = STR_NULL;

    StringCopy(szEncrypt, szTemp);

    //-------------------------------------

    StringCopy(lpszCrypted, szEncrypt);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Verify that a password matches its encrypted representation.
 * @param lpszCrypted Encrypted password.
 * @param lpszPassword Plain text password to check.
 * @return TRUE if passwords match.
 */
BOOL CheckPassword(LPCSTR lpszCrypted, LPCSTR lpszPassword) {
    STR szPass[48];
    STR szEncrypt[48];
    STR szDecrypt[48];
    STR szTemp[48];
    U32 PassLength, EncryptLength;
    U32 Hash, CurrentBit, c;
    BOOL Result = FALSE;

    if (StringLength(lpszCrypted) > 32) return FALSE;
    if (StringLength(lpszPassword) > 32) return FALSE;

    //-------------------------------------
    // Copy the strings

    StringCopy(szPass, lpszPassword);
    StringCopy(szEncrypt, lpszCrypted);

    //-------------------------------------
    // Make the password uppercase

    StringToUpper(szPass);

    //-------------------------------------
    // Check if lengths are consistent

    PassLength = StringLength(szPass);
    EncryptLength = StringLength(szEncrypt);

    if (PassLength != EncryptLength) return 0;

    //-------------------------------------
    // Reverse the string

    for (c = 0; c < PassLength; c++) {
        szTemp[(PassLength - 1) - c] = szEncrypt[c];
    }

    szTemp[c] = STR_NULL;

    StringCopy(szEncrypt, szTemp);

    //-------------------------------------
    // Compute the hashcode of the given password

    Hash = 0;
    for (c = 0; c < PassLength; c++) Hash = (Hash << 1) + szPass[c];

    //-------------------------------------
    // Decrypt the crypted password using the hashcode
    // If the bit in the hashcode corresponding to a character is set,
    // XOR the character with 0xAA else XOR it with 0x55

    for (c = 0; c < PassLength; c++) {
        CurrentBit = (Hash & ((U32)1 << c)) >> c;

        switch (CurrentBit) {
            case 0:
                szDecrypt[c] = (szEncrypt[c] ^ 0x55);
                break;
            case 1:
                szDecrypt[c] = (szEncrypt[c] ^ 0xAA);
                break;
        }
    }

    szDecrypt[c] = STR_NULL;

    //-------------------------------------
    // Now compare the decrypted password and the given password

    if (StringCompare(szPass, szDecrypt) == 0) Result = TRUE;

    //-------------------------------------
    // Clear all the strings just in case

    MemorySet(szPass, 0, sizeof szPass);
    MemorySet(szEncrypt, 0, sizeof szEncrypt);
    MemorySet(szDecrypt, 0, sizeof szDecrypt);
    MemorySet(szTemp, 0, sizeof szTemp);

    return Result;
}

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

/***************************************************************************/
