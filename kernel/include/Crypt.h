
// Crypt.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

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
