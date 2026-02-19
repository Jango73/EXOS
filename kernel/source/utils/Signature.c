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
#include "monocypher-ed25519.h"

/***************************************************************************/

/**
 * @brief Convert an EXOS U64 value to U32 when it fits.
 * @param Value Source value.
 * @param Out Receives converted U32 value.
 * @return TRUE when conversion is exact.
 */
static BOOL SignatureU64ToU32(U64 Value, U32* Out) {
    if (Out == NULL) {
        return FALSE;
    }

    if (U64_High32(Value) != 0) {
        return FALSE;
    }

    *Out = U64_Low32(Value);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Validate an offset/size range against a blob size.
 * @param Offset64 Start offset inside blob.
 * @param Size64 Number of bytes.
 * @param BlobSize Total blob size.
 * @param OffsetOut Receives converted offset.
 * @param SizeOut Receives converted size.
 * @return TRUE when the range is valid.
 */
static BOOL SignatureExtractRange(U64 Offset64,
                                  U64 Size64,
                                  U32 BlobSize,
                                  U32* OffsetOut,
                                  U32* SizeOut) {
    U32 Offset;
    U32 Size;
    U32 End;

    if (!SignatureU64ToU32(Offset64, &Offset)) {
        return FALSE;
    }

    if (!SignatureU64ToU32(Size64, &Size)) {
        return FALSE;
    }

    if (Offset > BlobSize) {
        return FALSE;
    }

    End = Offset + Size;
    if (End < Offset) {
        return FALSE;
    }

    if (End > BlobSize) {
        return FALSE;
    }

    if (OffsetOut != NULL) {
        *OffsetOut = Offset;
    }

    if (SizeOut != NULL) {
        *SizeOut = Size;
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
    static const U8 EmptyPayload = 0;
    const U8* PayloadBytes;

    if (Payload == NULL && PayloadSize != 0) {
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

    if (Payload == NULL) {
        PayloadBytes = &EmptyPayload;
    } else {
        PayloadBytes = (const U8*)Payload;
    }

    if (Algorithm == SIGNATURE_ALGORITHM_ED25519) {
        if (PublicKeySize != SIGNATURE_ED25519_PUBLIC_KEY_SIZE ||
            SignatureSize != SIGNATURE_ED25519_SIGNATURE_SIZE) {
            return SIGNATURE_STATUS_FORMAT_ERROR;
        }

        if (crypto_ed25519_check((const U8*)Signature,
                                 (const U8*)PublicKey,
                                 PayloadBytes,
                                 (size_t)PayloadSize) == 0) {
            return SIGNATURE_STATUS_OK;
        }

        return SIGNATURE_STATUS_INVALID_SIGNATURE;
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
    U32 PublicKeyOffset;
    U32 PublicKeySize;
    U32 SignatureOffset;
    U32 SignatureSize;
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

    if (!SignatureExtractRange(Header->PublicKeyOffset,
                               Header->PublicKeySize,
                               BlobSize,
                               &PublicKeyOffset,
                               &PublicKeySize)) {
        return SIGNATURE_STATUS_FORMAT_ERROR;
    }

    if (!SignatureExtractRange(Header->SignatureOffset,
                               Header->SignatureSize,
                               BlobSize,
                               &SignatureOffset,
                               &SignatureSize)) {
        return SIGNATURE_STATUS_FORMAT_ERROR;
    }

    if (PublicKeySize == 0) {
        PublicKey = NULL;
    } else {
        PublicKey = BlobBytes + PublicKeyOffset;
    }

    if (SignatureSize == 0) {
        Signature = NULL;
    } else {
        Signature = BlobBytes + SignatureOffset;
    }

    return SignatureVerifyDetached(Header->Algorithm,
                                   PublicKey,
                                   PublicKeySize,
                                   Payload,
                                   PayloadSize,
                                   Signature,
                                   SignatureSize);
}
