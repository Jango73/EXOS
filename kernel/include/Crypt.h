
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
#ifndef CRYPT_H_INCLUDED
#define CRYPT_H_INCLUDED

#include "Base.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

BOOL MakePassword(LPCSTR, LPSTR);
BOOL CheckPassword(LPCSTR, LPCSTR);

/***************************************************************************/

typedef struct _CRC32_CTX {
    U32 State; /* running CRC (already includes init/final-xor handling) */
} CRC32_CTX, *LPCRC32_CTX;

/* Streaming API */
void CRC32Begin(LPCRC32_CTX Ctx);
void CRC32Update(LPCRC32_CTX Ctx, const void *Data, U32 Length);
U32 CRC32Final(LPCRC32_CTX Ctx);

/* One-shot API */
U32 CRC32(const void *Data, U32 Length);

/***************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
