
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


    Bcrypt - Unit Tests

\************************************************************************/

#include "Autotest.h"
#include "Base.h"
#include "Heap.h"
#include "Log.h"
#include "CoreString.h"
#include "System.h"

/************************************************************************/

// Bcrypt constants and types (avoiding system includes)
#define MAXKEYBYTES 56
#define ENCRYPT 0
#define DECRYPT 1

typedef unsigned long uLong;

typedef struct {
    unsigned char remove;
    unsigned char standardout;
    unsigned char compression;
    unsigned char type;
    uLong origsize;
    unsigned char securedelete;
} BCoptions;

/************************************************************************/
// Function declarations for BFEncrypt/BFDecrypt

uLong BFEncrypt(char **input, char *key, uLong sz, BCoptions *options);
uLong BFDecrypt(char **input, char *key, char *key2, uLong sz, BCoptions *options);

/************************************************************************/

/**
 * @brief Helper function to test encryption and decryption with given data.
 *
 * Tests both BFEncrypt and BFDecrypt functions to ensure they work correctly
 * together. Encrypts the input data and then decrypts it, verifying that
 * the decrypted result matches the original input.
 *
 * @param TestName Name of the test case for logging purposes
 * @param OriginalData Original data to encrypt and decrypt
 * @param DataSize Size of the original data in bytes
 * @param Key Encryption key to use
 * @return TRUE if encryption/decryption cycle succeeds, FALSE otherwise
 */
static BOOL TestEncryptDecrypt(const char *TestName, const char *OriginalData, U32 DataSize, const char *Key, TEST_RESULTS* Results) {
    BCoptions Options = {0};
    char *BufferPtr = NULL;
    char *InitialBuffer = NULL;
    char PrimaryKey[MAXKEYBYTES];
    char SecondaryKey[MAXKEYBYTES];
    uLong EncryptedSize = 0;
    uLong DecryptedSize = 0;
    U32 AllocationSize = 0;
    U32 PayloadSize = 0;
    U32 PaddingSize = 0;
    U32 WorkingSize = 0;
    U32 KeyLength = 0;
    BOOL TestPassed = FALSE;
    const U32 BlockSize = (U32)(sizeof(uInt32) * 2U);

    DEBUG(TEXT("[TestBcrypt] Starting test: %s"), TestName);
    DEBUG(TEXT("[TestBcrypt] Data size: %u"), DataSize);

    PayloadSize = DataSize + MAXKEYBYTES;
    if (PayloadSize < BlockSize) {
        PaddingSize = BlockSize - PayloadSize;
    } else {
        U32 Remainder = PayloadSize % BlockSize;
        if (Remainder != 0U) {
            PaddingSize = BlockSize - Remainder;
        }
    }
    WorkingSize = PayloadSize + PaddingSize;
    AllocationSize = WorkingSize + 16U;

    DEBUG(TEXT("[TestBcrypt] Payload size: %u"), PayloadSize);
    DEBUG(TEXT("[TestBcrypt] Padding size: %u"), PaddingSize);
    DEBUG(TEXT("[TestBcrypt] Working size (aligned): %u"), WorkingSize);
    DEBUG(TEXT("[TestBcrypt] Allocation size with headroom: %u"), AllocationSize);

    BufferPtr = (char *)KernelHeapAlloc(AllocationSize);
    if (BufferPtr == NULL) {
        DEBUG(TEXT("[TestBcrypt] KernelHeapAlloc failed"));
        goto cleanup;
    }

    MemorySet(BufferPtr, 0, AllocationSize);
    InitialBuffer = BufferPtr;
    DEBUG(TEXT("[TestBcrypt] Allocated buffer at: %p"), InitialBuffer);

    MemorySet(PrimaryKey, 0, sizeof(PrimaryKey));
    MemorySet(SecondaryKey, 0, sizeof(SecondaryKey));
    KeyLength = StringLength(Key);
    if (KeyLength > MAXKEYBYTES) {
        KeyLength = MAXKEYBYTES;
    }
    MemoryCopy(PrimaryKey, Key, KeyLength);
    MemoryCopy(SecondaryKey, PrimaryKey, MAXKEYBYTES);

    MemoryCopy(BufferPtr, OriginalData, DataSize);
    MemoryCopy(BufferPtr + DataSize, PrimaryKey, MAXKEYBYTES);

    DEBUG(TEXT("[TestBcrypt] Buffer pointer before encrypt call: %p"), BufferPtr);

    Options.remove = 0;
    Options.standardout = 0;
    Options.compression = 0;
    Options.type = ENCRYPT;
    Options.origsize = 0;
    Options.securedelete = 0;

    DEBUG(TEXT("[TestBcrypt] Options struct address: %p"), &Options);
    DEBUG(TEXT("[TestBcrypt] Calling BFEncrypt with pointer: %p, key: %p, size: %u"), BufferPtr, PrimaryKey, WorkingSize);
    EncryptedSize = BFEncrypt(&BufferPtr, PrimaryKey, WorkingSize, &Options);
    if (EncryptedSize == 0) {
        DEBUG(TEXT("[TestBcrypt] Encryption failed for test: %s"), TestName);
        goto cleanup;
    }

    DEBUG(TEXT("[TestBcrypt] Encryption finished, size: %u"), (U32)EncryptedSize);
    DEBUG(TEXT("[TestBcrypt] Buffer pointer after encrypt: %p"), BufferPtr);

    DEBUG(TEXT("[TestBcrypt] Calling BFDecrypt with pointer: %p, key: %p, key2: %p, size: %u"), BufferPtr, PrimaryKey, SecondaryKey, (U32)EncryptedSize);
    DecryptedSize = BFDecrypt(&BufferPtr, PrimaryKey, SecondaryKey, EncryptedSize, &Options);
    if (DecryptedSize == 0) {
        DEBUG(TEXT("[TestBcrypt] Decryption failed for test: %s"), TestName);
        goto cleanup;
    }

    DEBUG(TEXT("[TestBcrypt] Decryption finished, size: %u"), (U32)DecryptedSize);
    DEBUG(TEXT("[TestBcrypt] Buffer pointer after decrypt: %p"), BufferPtr);

    if (DataSize == 0U) {
        TestPassed = TRUE;
    } else if ((DecryptedSize >= DataSize) && (MemoryCompare(BufferPtr, OriginalData, DataSize) == 0)) {
        TestPassed = TRUE;
    } else {
        DEBUG(TEXT("[TestBcrypt] Data verification failed for test: %s"), TestName);
        DEBUG(TEXT("[TestBcrypt] Expected size: %u, Got size: %u"), DataSize, (U32)DecryptedSize);
    }

cleanup:
    if (BufferPtr != NULL) {
        KernelHeapFree(BufferPtr);
        BufferPtr = NULL;
    }

    Results->TestsRun++;
    if (TestPassed) {
        Results->TestsPassed++;
    }

    DEBUG(TEXT("[TestBcrypt] Test result for %s: %s"), TestName, TestPassed ? TEXT("PASS") : TEXT("FAIL"));
    return TestPassed;
}

/**
 * @brief Comprehensive unit test for Bcrypt encryption/decryption functionality.
 *
 * Tests various data patterns and sizes to ensure the BFEncrypt and BFDecrypt
 * functions work correctly together. Includes tests for empty data, short strings,
 * longer text, and binary-like data.
 *
 * @return TRUE if all bcrypt tests pass, FALSE if any test fails
 */
void TestBcrypt(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    DEBUG(TEXT("[TestBcrypt] Starting bcrypt autotests"));

    // Test 1: Simple short string
    TestEncryptDecrypt("Simple string", "Hello World!", 12, "mypassword123456", Results);

    // Test 2: Single character
    TestEncryptDecrypt("Single char", "A", 1, "singlekey1234567", Results);

    // Test 3: Longer text sample
    TestEncryptDecrypt("Long text", "The quick brown fox jumps over the lazy dog.", 44, "longkey123456789", Results);

    // Test 4: Text with special characters
    TestEncryptDecrypt("Special chars", "!@#$%^&*()_+-=[]{}|;:,.<>?", 26, "specialkey123456", Results);

    // Test 5: Numeric string
    TestEncryptDecrypt("Numeric", "1234567890", 10, "numkey1234567890", Results);

    // Test 6: Binary-like data (with null bytes)
    char BinaryData[] = {0x01, 0x02, 0x00, 0x03, 0x04, 0xFF, 0x00, 0x05};
    TestEncryptDecrypt("Binary data", BinaryData, 8, "binarykey1234567", Results);

    DEBUG(TEXT("[TestBcrypt] Completed bcrypt autotests. Tests run: %u, passed: %u"), Results->TestsRun, Results->TestsPassed);
}
