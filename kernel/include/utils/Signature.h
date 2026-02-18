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


    Detached signature helpers

\************************************************************************/

#ifndef SIGNATURE_H_INCLUDED
#define SIGNATURE_H_INCLUDED

#include "Base.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

#define SIGNATURE_ALGORITHM_NONE 0
#define SIGNATURE_ALGORITHM_ED25519 1
#define SIGNATURE_ALGORITHM_RSA_PKCS1_V15_SHA256 2

#define SIGNATURE_STATUS_OK 0
#define SIGNATURE_STATUS_INVALID_ARGUMENT 1
#define SIGNATURE_STATUS_FORMAT_ERROR 2
#define SIGNATURE_STATUS_UNSUPPORTED_ALGORITHM 3
#define SIGNATURE_STATUS_INVALID_SIGNATURE 4
#define SIGNATURE_STATUS_INTERNAL_ERROR 5

#define DETACHED_SIGNATURE_MAGIC 0x53474953
#define DETACHED_SIGNATURE_VERSION 1

/***************************************************************************/

typedef struct PACKED tag_DETACHED_SIGNATURE_HEADER {
    U32 Magic;
    U32 Version;
    U32 Algorithm;
    U32 Reserved;
    U64 PublicKeyOffset;
    U64 PublicKeySize;
    U64 SignatureOffset;
    U64 SignatureSize;
} DETACHED_SIGNATURE_HEADER;

/***************************************************************************/

U32 SignatureVerifyDetached(U32 Algorithm,
                            const void* PublicKey,
                            U32 PublicKeySize,
                            const void* Payload,
                            U32 PayloadSize,
                            const void* Signature,
                            U32 SignatureSize);

U32 SignatureVerifyDetachedBlob(const void* Blob,
                                U32 BlobSize,
                                const void* Payload,
                                U32 PayloadSize);

/***************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
