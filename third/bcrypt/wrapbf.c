/* ====================================================================
 * Copyright (c) 2002 Johnny Shelley.  All rights reserved.
 *
 * Bcrypt is licensed under the BSD software license. See the file
 * called 'LICENSE' that you should have received with this software
 * for details
 * ====================================================================
 */

#include "includes.h"
#include "defines.h"
#include "functions.h"
#include "Base.h"
#include "Log.h"

uLong BFEncrypt(char **input, char *key, uLong sz, BCoptions *options) {
  uInt32 L, R;
  uLong i;
  BLOWFISH_CTX ctx;
  int j;
  unsigned char *myEndian = NULL;
  char *initialBuffer = NULL;
  j = sizeof(uInt32);

  if (input != NULL) {
    initialBuffer = *input;
  }

  DEBUG(TEXT("[BFEncrypt] Enter (input pointer: %p, buffer: %p, size: %u, options: %p)"),
        (void *)input,
        (void *)initialBuffer,
        (U32)sz,
        (void *)options);
  DEBUG(TEXT("[BFEncrypt] Key pointer: %p"), (void *)key);
  DEBUG(TEXT("[BFEncrypt] Block span: %u"), (U32)(j * 2));
  if (options != NULL) {
    DEBUG(TEXT("[BFEncrypt] Options remove=%u stdout=%u compression=%u type=%u origsize=%u securedelete=%u"),
          (U32)options->remove,
          (U32)options->standardout,
          (U32)options->compression,
          (U32)options->type,
          (U32)options->origsize,
          (U32)options->securedelete);
  }

  getEndian(&myEndian);

  DEBUG(TEXT("[BFEncrypt] getEndian produced pointer: %p"), (void *)myEndian);
  if (myEndian != NULL) {
    DEBUG(TEXT("[BFEncrypt] Endian flag value: %x"), (U32)myEndian[0]);
  }

  DEBUG(TEXT("[BFEncrypt] Preparing to shift buffer: source=%p dest=%p length=%u"),
        (void *)initialBuffer,
        initialBuffer != NULL ? (void *)(initialBuffer + 2) : NULL,
        (U32)sz);

  memmove(*input+2, *input, sz);

  memcpy(*input, myEndian, 1);
  memcpy(*input+1, &options->compression, 1);

  sz += 2;    /* add room for endian and compress flags */

  DEBUG(TEXT("[BFEncrypt] Header written, buffer=%p, size=%u"), (void *)*input, (U32)sz);

  Blowfish_Init (&ctx, key, MAXKEYBYTES);

  DEBUG(TEXT("[BFEncrypt] Blowfish context initialized"));

  for (i = 2; i < sz; i+=(j*2)) {       /* start just after tags */
    DEBUG(TEXT("[BFEncrypt] Processing block offset=%u"), (U32)i);
    memcpy(&L, *input+i, j);
    memcpy(&R, *input+i+j, j);
    DEBUG(TEXT("[BFEncrypt] Pre-encrypt L=%x R=%x"), L, R);
    Blowfish_Encrypt(&ctx, &L, &R);
    memcpy(*input+i, &L, j);
    memcpy(*input+i+j, &R, j);
    DEBUG(TEXT("[BFEncrypt] Post-encrypt L=%x R=%x"), L, R);
  }

  if (options->compression == 1) {
    DEBUG(TEXT("[BFEncrypt] Compression flag set, resizing buffer (current size=%u)"), (U32)sz);
    if ((*input = realloc(*input, sz + j + 1)) == NULL)
      memerror();

    DEBUG(TEXT("[BFEncrypt] Buffer reallocated, new pointer: %p"), (void *)*input);

    memset(*input+sz, 0, j + 1);
    memcpy(*input+sz, &options->origsize, j);
    sz += j;  /* make room for the original size      */

    DEBUG(TEXT("[BFEncrypt] Stored original size tag=%u resulting size=%u"),
          (U32)options->origsize,
          (U32)sz);
  }

  free(myEndian);
  DEBUG(TEXT("[BFEncrypt] Leaving function with buffer=%p final size=%u"), (void *)*input, (U32)sz);
  return(sz);
}

uLong BFDecrypt(char **input, char *key, char *key2, uLong sz,
        BCoptions *options) {
  uInt32 L, R;
  uLong i;
  BLOWFISH_CTX ctx;
  int j, swap = 0;
  unsigned char *myEndian = NULL;
  char *mykey = NULL;
  char *initialBuffer = NULL;

  j = sizeof(uInt32);

  if (input != NULL) {
    initialBuffer = *input;
  }

  DEBUG(TEXT("[BFDecrypt] Enter (input pointer: %p, buffer: %p, size: %u, options: %p)"),
        (void *)input,
        (void *)initialBuffer,
        (U32)sz,
        (void *)options);
  DEBUG(TEXT("[BFDecrypt] Primary key pointer: %p secondary key pointer: %p"), (void *)key, (void *)key2);
  DEBUG(TEXT("[BFDecrypt] Block span: %u"), (U32)(j * 2));

  if ((mykey = malloc(MAXKEYBYTES + 1)) == NULL)
    memerror();

  memset(mykey, 0, MAXKEYBYTES + 1);

  DEBUG(TEXT("[BFDecrypt] Scratch key buffer allocated at %p"), (void *)mykey);

  if ((swap = testEndian(*input)) == 1)
    memcpy(mykey, key2, MAXKEYBYTES);
  else
    memcpy(mykey, key, MAXKEYBYTES);

  DEBUG(TEXT("[BFDecrypt] testEndian result=%u"), (U32)swap);
  DEBUG(TEXT("[BFDecrypt] Selected key contents pointer=%p"), (void *)mykey);

  memcpy(&options->compression, *input+1, 1);

  DEBUG(TEXT("[BFDecrypt] Compression flag read=%u"), (U32)options->compression);

  if (options->compression == 1) {
    DEBUG(TEXT("[BFDecrypt] Compression metadata present, size before trim=%u"), (U32)sz);
    memcpy(&options->origsize, *input+(sz - j), j);
    sz -= j;  /* dump the size tag    */

    DEBUG(TEXT("[BFDecrypt] Restored original size tag=%u new size=%u"),
          (U32)options->origsize,
          (U32)sz);
  }

  sz -= 2;    /* now dump endian and compress flags   */

  DEBUG(TEXT("[BFDecrypt] Stripped header bytes, payload size=%u"), (U32)sz);

  Blowfish_Init (&ctx, mykey, MAXKEYBYTES);

  DEBUG(TEXT("[BFDecrypt] Blowfish context initialized"));

  for (i = 0; i < sz; i+=(j*2)) {
    DEBUG(TEXT("[BFDecrypt] Processing block offset=%u"), (U32)i);
    memcpy(&L, *input+i+2, j);
    memcpy(&R, *input+i+j+2, j);

    DEBUG(TEXT("[BFDecrypt] Pre-decrypt L=%x R=%x"), L, R);

    if (swap == 1) {
      L = swapEndian(L);
      R = swapEndian(R);
      DEBUG(TEXT("[BFDecrypt] Swapped endian L=%x R=%x"), L, R);
    }

    Blowfish_Decrypt(&ctx, &L, &R);

    if (swap == 1) {
      L = swapEndian(L);
      R = swapEndian(R);
      DEBUG(TEXT("[BFDecrypt] Restored endian L=%x R=%x"), L, R);
    }

    memcpy(*input+i, &L, j);
    memcpy(*input+i+j, &R, j);
    DEBUG(TEXT("[BFDecrypt] Post-decrypt block stored L=%x R=%x"), L, R);
  }

  while (memcmp(*input+(sz-1), "\0", 1) == 0) { /* strip excess nulls   */
    DEBUG(TEXT("[BFDecrypt] Trailing null detected at offset=%u"), (U32)(sz - 1));
    sz--;                                       /* from decrypted files */
  }

  DEBUG(TEXT("[BFDecrypt] Size after trimming nulls=%u"), (U32)sz);

  sz -= MAXKEYBYTES;

  DEBUG(TEXT("[BFDecrypt] Size after removing key trailer=%u"), (U32)sz);

  if (memcmp(*input+sz, mykey, MAXKEYBYTES) != 0) {
    DEBUG(TEXT("[BFDecrypt] Key verification failed"));
    return(0);
  }

  free(mykey);
  free(myEndian);
  DEBUG(TEXT("[BFDecrypt] Leaving function with buffer=%p final size=%u"), (void *)*input, (U32)sz);
  return(sz);
}
