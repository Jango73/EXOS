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

#include "utils/Signature.h"

#include "CoreString.h"

/***************************************************************************/

/**
 * @brief Validate an offset/size range against a blob size.
 * @param Offset Start offset inside blob.
 * @param Size Number of bytes.
 * @param BlobSize Total blob size.
 * @return TRUE when the range is valid.
 */
static BOOL SignatureIsRangeValid(U64 Offset, U64 Size, U32 BlobSize) {
    U64 End;

    if (Offset > (U64)BlobSize) {
        return FALSE;
    }

    End = Offset + Size;
    if (End < Offset) {
        return FALSE;
    }

    if (End > (U64)BlobSize) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Verify a detached signature with algorithm-specific backend.
 * @param Algorithm Signature algorithm identifier.
 * @param PublicKey Public key bytes.
 * @param PublicKeySize Public key byte size.
 * @param Payload Signed payload bytes.
 * @param PayloadSize Signed payload byte size.
 * @param Signature Signature bytes.
 * @param SignatureSize Signature byte size.
 * @return Signature verification status code.
 */
U32 SignatureVerifyDetached(U32 Algorithm,
                            const void* PublicKey,
                            U32 PublicKeySize,
                            const void* Payload,
                            U32 PayloadSize,
                            const void* Signature,
                            U32 SignatureSize) {
    if (Payload == NULL || PayloadSize == 0) {
        return SIGNATURE_STATUS_INVALID_ARGUMENT;
    }

    if (Algorithm == SIGNATURE_ALGORITHM_NONE) {
        if (PublicKeySize != 0 || SignatureSize != 0) {
            return SIGNATURE_STATUS_FORMAT_ERROR;
        }

        if (PublicKey != NULL || Signature != NULL) {
            return SIGNATURE_STATUS_FORMAT_ERROR;
        }

        return SIGNATURE_STATUS_OK;
    }

    if (PublicKey == NULL || Signature == NULL || PublicKeySize == 0 || SignatureSize == 0) {
        return SIGNATURE_STATUS_INVALID_ARGUMENT;
    }

    if (Algorithm == SIGNATURE_ALGORITHM_ED25519) {
        // Backend not wired yet: keep stable API so implementation can be swapped.
        return SIGNATURE_STATUS_UNSUPPORTED_ALGORITHM;
    }

    if (Algorithm == SIGNATURE_ALGORITHM_RSA_PKCS1_V15_SHA256) {
        // Backend not wired yet: keep stable API so implementation can be swapped.
        return SIGNATURE_STATUS_UNSUPPORTED_ALGORITHM;
    }

    return SIGNATURE_STATUS_UNSUPPORTED_ALGORITHM;
}

/***************************************************************************/

/**
 * @brief Verify a detached signature blob against a payload.
 * @param Blob Detached signature blob bytes.
 * @param BlobSize Detached signature blob size.
 * @param Payload Signed payload bytes.
 * @param PayloadSize Signed payload byte size.
 * @return Signature verification status code.
 */
U32 SignatureVerifyDetachedBlob(const void* Blob,
                                U32 BlobSize,
                                const void* Payload,
                                U32 PayloadSize) {
    const U8* BlobBytes;
    const DETACHED_SIGNATURE_HEADER* Header;
    const void* PublicKey;
    const void* Signature;

    if (Blob == NULL || BlobSize < sizeof(DETACHED_SIGNATURE_HEADER)) {
        return SIGNATURE_STATUS_FORMAT_ERROR;
    }

    BlobBytes = (const U8*)Blob;
    Header = (const DETACHED_SIGNATURE_HEADER*)BlobBytes;

    if (Header->Magic != DETACHED_SIGNATURE_MAGIC) {
        return SIGNATURE_STATUS_FORMAT_ERROR;
    }

    if (Header->Version != DETACHED_SIGNATURE_VERSION) {
        return SIGNATURE_STATUS_FORMAT_ERROR;
    }

    if (Header->Reserved != 0) {
        return SIGNATURE_STATUS_FORMAT_ERROR;
    }

    if (!SignatureIsRangeValid(Header->PublicKeyOffset, Header->PublicKeySize, BlobSize)) {
        return SIGNATURE_STATUS_FORMAT_ERROR;
    }

    if (!SignatureIsRangeValid(Header->SignatureOffset, Header->SignatureSize, BlobSize)) {
        return SIGNATURE_STATUS_FORMAT_ERROR;
    }

    if (Header->PublicKeySize == 0) {
        PublicKey = NULL;
    } else {
        PublicKey = BlobBytes + (U32)Header->PublicKeyOffset;
    }

    if (Header->SignatureSize == 0) {
        Signature = NULL;
    } else {
        Signature = BlobBytes + (U32)Header->SignatureOffset;
    }

    return SignatureVerifyDetached(Header->Algorithm,
                                   PublicKey,
                                   (U32)Header->PublicKeySize,
                                   Payload,
                                   PayloadSize,
                                   Signature,
                                   (U32)Header->SignatureSize);
}
