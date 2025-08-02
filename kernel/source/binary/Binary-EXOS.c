/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../../include/binary/Binary-EXOS.h"
#include "../../include/Console.h"
#include "../../include/File.h"

/***************************************************************************/

BOOL GetExecutableInfo_EXOS(LPFILE File, LPEXECUTABLEINFO Info) {
    FILEOPERATION FileOperation;
    EXOSCHUNK Chunk;
    EXOSHEADER Header;
    EXOSCHUNK_INIT Init;
    U32 BytesRead;
    U32 Index;
    U32 Dummy;

#ifdef __DEBUG__
    KernelPrint("Entering GetExecutableInfo_EXOS\n");
#endif

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    FileOperation.Size = sizeof(FILEOPERATION);
    FileOperation.File = (HANDLE)File;

    //-------------------------------------
    // Read the header

    FileOperation.NumBytes = sizeof(EXOSHEADER);
    FileOperation.Buffer = (LPVOID)&Header;
    BytesRead = ReadFile(&FileOperation);

    if (Header.Signature != EXOS_SIGNATURE) {
#ifdef __DEBUG__
        KernelPrint("GetExecutableInfo_EXOS() : Bad signature (%08X)\n",
                    Header.Signature);
#endif

        goto Out_Error;
    }

    while (1) {
        FileOperation.NumBytes = sizeof(EXOSCHUNK);
        FileOperation.Buffer = (LPVOID)&Chunk;
        BytesRead = ReadFile(&FileOperation);

        if (BytesRead != sizeof(EXOSCHUNK)) break;

        if (Chunk.ID == EXOS_CHUNK_INIT) {
            FileOperation.NumBytes = sizeof(EXOSCHUNK_INIT);
            FileOperation.Buffer = (LPVOID)&Init;
            BytesRead = ReadFile(&FileOperation);

            if (BytesRead != sizeof(EXOSCHUNK_INIT)) goto Out_Error;

            Info->EntryPoint = Init.EntryPoint;
            Info->CodeBase = Init.CodeBase;
            Info->DataBase = Init.DataBase;
            Info->CodeSize = Init.CodeSize;
            Info->DataSize = Init.DataSize;
            Info->StackMinimum = Init.StackMinimum;
            Info->StackRequested = Init.StackRequested;
            Info->HeapMinimum = Init.HeapMinimum;
            Info->HeapRequested = Init.HeapRequested;

            goto Out_Success;
        } else {
            for (Index = 0; Index < Chunk.Size; Index++) {
                FileOperation.NumBytes = 1;
                FileOperation.Buffer = (LPVOID)&Dummy;
                BytesRead = ReadFile(&FileOperation);
            }
        }
    }

Out_Success:

#ifdef __DEBUG__
    KernelPrint("Exiting GetExecutableInfo_EXOS (Success)\n");
#endif

    return TRUE;

Out_Error:

#ifdef __DEBUG__
    KernelPrint("Exiting GetExecutableInfo_EXOS (Error)\n");
#endif

    return FALSE;
}

/***************************************************************************/

BOOL LoadExecutable_EXOS(LPFILE File, LPEXECUTABLEINFO Info, LINEAR CodeBase,
                         LINEAR DataBase) {
    FILEOPERATION FileOperation;
    EXOSCHUNK Chunk;
    EXOSHEADER Header;
    EXOSCHUNK_FIXUP Fixup;
    LINEAR ItemAddress;
    U32 BytesRead;
    U32 Index;
    U32 CodeRead;
    U32 DataRead;
    U32 CodeOffset;
    U32 DataOffset;
    U32 NumFixups;
    U32 Dummy;
    U32 c;

#ifdef __DEBUG__
    KernelPrint("Entering LoadExecutable_EXOS\n");
#endif

    if (File == NULL) return FALSE;

    FileOperation.Size = sizeof(FILEOPERATION);
    FileOperation.File = (HANDLE)File;

    CodeRead = 0;
    DataRead = 0;

    CodeOffset = CodeBase - Info->CodeBase;
    DataOffset = DataBase - Info->DataBase;

#ifdef __DEBUG__
    KernelPrint("LoadExecutable_EXOS() : CodeBase = %08X\n", CodeBase);
    KernelPrint("LoadExecutable_EXOS() : DataBase = %08X\n", DataBase);
#endif

    //-------------------------------------
    // Read the header

    FileOperation.NumBytes = sizeof(EXOSHEADER);
    FileOperation.Buffer = (LPVOID)&Header;
    BytesRead = ReadFile(&FileOperation);

    if (Header.Signature != EXOS_SIGNATURE) {
        goto Out_Error;
    }

    while (1) {
        FileOperation.NumBytes = sizeof(EXOSCHUNK);
        FileOperation.Buffer = (LPVOID)&Chunk;
        BytesRead = ReadFile(&FileOperation);

        if (BytesRead != sizeof(EXOSCHUNK)) break;

        if (Chunk.ID == EXOS_CHUNK_CODE) {
            if (CodeRead == 1) {
                //-------------------------------------
                // Only one code chunk allowed

                goto Out_Error;
            }

#ifdef __DEBUG__
            KernelPrint("LoadExecutable_EXOS() : Reading code\n");
#endif

            FileOperation.NumBytes = Chunk.Size;
            FileOperation.Buffer = (LPVOID)CodeBase;
            BytesRead = ReadFile(&FileOperation);

            if (BytesRead != Chunk.Size) goto Out_Error;

            CodeRead = 1;
        } else if (Chunk.ID == EXOS_CHUNK_DATA) {
            if (DataRead == 1) {
                //-------------------------------------
                // Only one data chunk allowed

                goto Out_Error;
            }

#ifdef __DEBUG__
            KernelPrint("LoadExecutable_EXOS() : Reading data\n");
#endif

            FileOperation.NumBytes = Chunk.Size;
            FileOperation.Buffer = (LPVOID)DataBase;
            BytesRead = ReadFile(&FileOperation);

            if (BytesRead != Chunk.Size) goto Out_Error;

            DataRead = 1;
        } else if (Chunk.ID == EXOS_CHUNK_FIXUP) {
            FileOperation.NumBytes = sizeof(U32);
            FileOperation.Buffer = (LPVOID)&NumFixups;
            BytesRead = ReadFile(&FileOperation);

            if (BytesRead != sizeof(U32)) goto Out_Error;

#ifdef __DEBUG__
            KernelPrint("LoadExecutable_EXOS() : Reading relocations\n");
#endif

            for (c = 0; c < NumFixups; c++) {
                FileOperation.NumBytes = sizeof(EXOSCHUNK_FIXUP);
                FileOperation.Buffer = (LPVOID)&Fixup;
                BytesRead = ReadFile(&FileOperation);

                if (BytesRead != sizeof(EXOSCHUNK_FIXUP)) goto Out_Error;

                if (Fixup.Section & EXOS_FIXUP_SOURCE_CODE) {
                    ItemAddress = CodeBase + (Fixup.Address - Info->CodeBase);
                } else if (Fixup.Section & EXOS_FIXUP_SOURCE_DATA) {
                    ItemAddress = DataBase + (Fixup.Address - Info->DataBase);
                } else {
                    ItemAddress = NULL;
                }

                if (ItemAddress != NULL) {
                    if (Fixup.Section & EXOS_FIXUP_DEST_CODE) {
                        *((U32*)ItemAddress) += CodeOffset;
                    } else if (Fixup.Section & EXOS_FIXUP_DEST_DATA) {
                        *((U32*)ItemAddress) += DataOffset;
                    }
                }
            }

            goto Out_Success;
        } else {
            for (Index = 0; Index < Chunk.Size; Index++) {
                FileOperation.NumBytes = 1;
                FileOperation.Buffer = (LPVOID)&Dummy;
                BytesRead = ReadFile(&FileOperation);
            }
        }
    }

    if (CodeRead == 0) goto Out_Error;

Out_Success:

#ifdef __DEBUG__
    KernelPrint("Exiting LoadExecutable_EXOS\n");
#endif

    return TRUE;

Out_Error:

#ifdef __DEBUG__
    KernelPrint("Exiting LoadExecutable_EXOS\n");
#endif

    return FALSE;
}

/***************************************************************************/
