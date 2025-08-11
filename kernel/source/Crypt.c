
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Crypt.h"

#include "../include/String.h"

/***************************************************************************/

/* Reflected polynomial for CRC-32 (IEEE 802.3) */
#define CRC32_POLY ((U32)0xEDB88320u)
#define CRC32_INIT ((U32)0xFFFFFFFFu)
#define CRC32_FINAL_XOR ((U32)0xFFFFFFFFu)

/***************************************************************************/

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

/* Process one byte with bitwise algorithm (LSB-first). */
static inline U32 CRC32_ProcessByte(U32 Crc, U8 Byte) {
    U32 c = Crc ^ (U32)Byte;
    for (UINT i = 0; i < 8; ++i) {
        U32 mask = (c & 1u) ? CRC32_POLY : 0u;
        c = (c >> 1) ^ mask;
    }
    return c;
}

/***************************************************************************/

/* Streaming API */
void CRC32Begin(LPCRC32_CTX Ctx) {
    /* State holds the "internal" CRC value (before final XOR). */
    Ctx->State = CRC32_INIT;
}

void CRC32Update(LPCRC32_CTX Ctx, const void *Data, U32 Length) {
    const U8 *p = (const U8 *)Data;
    U32 c = Ctx->State;

    while (Length--) {
        c = CRC32_ProcessByte(c, *p++);
    }

    Ctx->State = c;
}

U32 CRC32Final(LPCRC32_CTX Ctx) {
    /* Apply final XOR and return; also store finalized value in context. */
    Ctx->State ^= CRC32_FINAL_XOR;
    return Ctx->State;
}

/***************************************************************************/

/* One-shot API */
U32 CRC32(const void *Data, U32 Length) {
    CRC32_CTX ctx;
    CRC32Begin(&ctx);
    CRC32Update(&ctx, Data, Length);
    return CRC32Final(&ctx);
}

/***************************************************************************/
