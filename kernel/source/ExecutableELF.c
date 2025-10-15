
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


    Executable ELF

\************************************************************************/

#include "ExecutableELF.h"

#include "Log.h"
#include "CoreString.h"

/************************************************************************/

// Packed structures (GCC/Clang).
typedef struct __attribute__((packed)) tag_EXOS_ELF32_EHDR {
    U8 e_ident[EI_NIDENT];
    U16 e_type;
    U16 e_machine;
    U32 e_version;
    U32 e_entry;
    U32 e_phoff;
    U32 e_shoff;
    U32 e_flags;
    U16 e_ehsize;
    U16 e_phentsize;
    U16 e_phnum;
    U16 e_shentsize;
    U16 e_shnum;
    U16 e_shstrndx;
} EXOS_ELF32_EHDR;

typedef struct __attribute__((packed)) tag_EXOS_ELF32_PHDR {
    U32 p_type;
    U32 p_offset;
    U32 p_vaddr;
    U32 p_paddr;
    U32 p_filesz;
    U32 p_memsz;
    U32 p_flags;
    U32 p_align;
} EXOS_ELF32_PHDR;

/************************************************************************/
// Local helpers

static U32 ELFMakeSig(const U8 ident[EI_NIDENT]) {
    return ((U32)ident[0]) | ((U32)ident[1] << 8) | ((U32)ident[2] << 16) | ((U32)ident[3] << 24);
}

static BOOL ELFIsCode(U32 flags) { return (flags & PF_X) != 0; }
static BOOL ELFIsData(U32 flags) { return (flags & PF_W) != 0 || ((flags & PF_X) == 0); }

/* Safe 32-bit range addition: returns FALSE on overflow */
static BOOL Add32Overflow(U32 a, U32 b, U32* out) {
    U32 r = a + b;
    if (r < a) return FALSE;
    if (out) *out = r;
    return TRUE;
}

/************************************************************************/
// GetExecutableInfo_ELF
// Reads ELF header and program headers, classifies segments, computes layout.
// COMMENTS & LOGS IN ENGLISH (per coding guideline)

BOOL GetExecutableInfo_ELF(LPFILE File, LPEXECUTABLEINFO Info) {
    FILEOPERATION FileOperation;
    EXOS_ELF32_EHDR Ehdr;
    EXOS_ELF32_PHDR Phdr;
    U32 FileSize;
    U32 Sig;
    U32 i;

    U32 CodeMin = 0xFFFFFFFFU, CodeMax = 0;
    U32 DataMin = 0xFFFFFFFFU, DataMax = 0;
    U32 BssMin = 0xFFFFFFFFU, BssMax = 0;
    BOOL HasLoadable = FALSE;
    BOOL HasCode = FALSE;
    BOOL HasInterp = FALSE;

    DEBUG(TEXT("Entering GetExecutableInfo_ELF"));

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    /* Initialize operation header */
    FileOperation.Header.Size = sizeof(FILEOPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;
    FileOperation.Buffer = NULL;
    FileOperation.NumBytes = 0;

    FileSize = GetFileSize(File);
    if (FileSize < sizeof(EXOS_ELF32_EHDR)) goto Out_Error;

    /* Read ELF header */
    FileOperation.NumBytes = 0; /* absolute offset 0 */
    if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

    FileOperation.Buffer = (LPVOID)&Ehdr;
    FileOperation.NumBytes = sizeof(EXOS_ELF32_EHDR);
    if (ReadFile(&FileOperation) != sizeof(EXOS_ELF32_EHDR)) goto Out_Error;

    /* Validate ELF basics */
    Sig = ELFMakeSig(Ehdr.e_ident);
    if (Sig != ELF_SIGNATURE) goto Out_Error;
    if (Ehdr.e_ident[EI_CLASS] != ELFCLASS32) goto Out_Error;
    if (Ehdr.e_ident[EI_DATA] != ELFDATA2LSB) goto Out_Error;
    if (Ehdr.e_version != EV_CURRENT) goto Out_Error;
    if (Ehdr.e_type != ET_EXEC) goto Out_Error;
    if (Ehdr.e_machine != EM_386) goto Out_Error;

    if (Ehdr.e_phnum == 0) goto Out_Error;
    if (Ehdr.e_phentsize < sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

    /* Bounds check Program Header Table area */
    {
        U32 pht_end;
        if (!Add32Overflow(Ehdr.e_phoff, (U32)Ehdr.e_phnum * (U32)Ehdr.e_phentsize, &pht_end)) goto Out_Error;
        if (pht_end > FileSize) goto Out_Error;
    }

    /* Iterate program headers to classify ranges */
    for (i = 0; i < (U32)Ehdr.e_phnum; ++i) {
        U32 phoff_i;
        U32 vend;
        BOOL IsCode;
        BOOL IsData;

        if (!Add32Overflow(Ehdr.e_phoff, i * (U32)Ehdr.e_phentsize, &phoff_i)) goto Out_Error;

        FileOperation.NumBytes = phoff_i;
        if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

        FileOperation.Buffer = (LPVOID)&Phdr;
        FileOperation.NumBytes = sizeof(EXOS_ELF32_PHDR);
        if (ReadFile(&FileOperation) != sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

        if (Phdr.p_type == PT_INTERP) {
            HasInterp = TRUE;
        }
        if (Phdr.p_type != PT_LOAD) continue;

        HasLoadable = TRUE;

        /* Sanity checks on sizes and ranges */
        if (!Add32Overflow(Phdr.p_vaddr, Phdr.p_memsz, &vend)) goto Out_Error; /* overflow */
        /* File range check (only for file portion) */
        {
            U32 fend;
            if (!Add32Overflow(Phdr.p_offset, Phdr.p_filesz, &fend)) goto Out_Error;
            if (fend > FileSize) goto Out_Error;
        }

        IsCode = ELFIsCode(Phdr.p_flags);
        IsData = ELFIsData(Phdr.p_flags);

        if (IsCode) {
            HasCode = TRUE;
            if (Phdr.p_vaddr < CodeMin) CodeMin = Phdr.p_vaddr;
            if (vend > CodeMax) CodeMax = vend;
        } else if (IsData) {
            if (Phdr.p_vaddr < DataMin) DataMin = Phdr.p_vaddr;
            if (vend > DataMax) DataMax = vend;
        } else {
            /* Read-only no-X segment -> treat as DATA */
            if (Phdr.p_vaddr < DataMin) DataMin = Phdr.p_vaddr;
            if (vend > DataMax) DataMax = vend;
        }

        /* Track BSS span if any (memsz > filesz) */
        if (Phdr.p_memsz > Phdr.p_filesz) {
            U32 bss_start, bss_end;
            if (!Add32Overflow(Phdr.p_vaddr, Phdr.p_filesz, &bss_start)) goto Out_Error;
            if (!Add32Overflow(Phdr.p_vaddr, Phdr.p_memsz, &bss_end)) goto Out_Error;
            if (bss_start < BssMin) BssMin = bss_start;
            if (bss_end > BssMax) BssMax = bss_end;
        }
    }

    if (!HasLoadable) goto Out_Error;
    if (!HasCode) goto Out_Error;  /* must have at least one executable segment */
    if (HasInterp) goto Out_Error; /* dynamic/ELF with interpreter not supported here */

    /* Populate Info (image-space) */
    Info->EntryPoint = Ehdr.e_entry;

    if (CodeMin != 0xFFFFFFFFU && CodeMax > CodeMin) {
        Info->CodeBase = CodeMin;
        Info->CodeSize = CodeMax - CodeMin;
    } else {
        Info->CodeBase = 0;
        Info->CodeSize = 0;
    }

    if (DataMin != 0xFFFFFFFFU && DataMax > DataMin) {
        Info->DataBase = DataMin;
        Info->DataSize = DataMax - DataMin;
    } else {
        Info->DataBase = 0;
        Info->DataSize = 0;
    }

    if (BssMin != 0xFFFFFFFFU && BssMax > BssMin) {
        Info->BssBase = BssMin;
        Info->BssSize = BssMax - BssMin;
    } else {
        Info->BssBase = 0;
        Info->BssSize = 0;
    }

    /* ELF does not carry stack/heap size requests in the file format */
    Info->StackMinimum = 0;
    Info->StackRequested = 0;
    Info->HeapMinimum = 0;
    Info->HeapRequested = 0;

    DEBUG(TEXT("Exiting GetExecutableInfo_ELF (success)"));
    return TRUE;

Out_Error:
    DEBUG(TEXT("Exiting GetExecutableInfo_ELF (error)"));
    return FALSE;
}

/************************************************************************/
// LoadExecutable_ELF
// Loads PT_LOAD segments into the provided base addresses, zero-fills BSS,
// and fixes up the effective entry point.
// COMMENTS & LOGS IN ENGLISH (per coding guideline)

BOOL LoadExecutable_ELF(LPFILE File, LPEXECUTABLEINFO Info, LINEAR CodeBase, LINEAR DataBase, LINEAR BssBase) {
    UNUSED(BssBase);

    FILEOPERATION FileOperation;
    EXOS_ELF32_EHDR Ehdr;
    EXOS_ELF32_PHDR Phdr;
    U32 FileSize;
    U32 i;

    U32 CodeRef = 0, DataRef = 0;
    U32 CodeMin = 0xFFFFFFFFU, CodeMax = 0;
    U32 DataMin = 0xFFFFFFFFU, DataMax = 0;
    BOOL HasCode = FALSE;

    DEBUG(TEXT("[LoadExecutable_ELF] %s"), File->Name);

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    /* Initialize operation header */
    FileOperation.Header.Size = sizeof(FILEOPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;
    FileOperation.Buffer = NULL;
    FileOperation.NumBytes = 0;

    FileSize = GetFileSize(File);
    if (FileSize < sizeof(EXOS_ELF32_EHDR)) goto Out_Error;

    /* Read ELF header */
    FileOperation.NumBytes = 0;
    if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

    FileOperation.Buffer = (LPVOID)&Ehdr;
    FileOperation.NumBytes = sizeof(EXOS_ELF32_EHDR);
    if (ReadFile(&FileOperation) != sizeof(EXOS_ELF32_EHDR)) goto Out_Error;

    /* Validate minimal fields again */
    if (ELFMakeSig(Ehdr.e_ident) != ELF_SIGNATURE) goto Out_Error;
    if (Ehdr.e_ident[EI_CLASS] != ELFCLASS32) goto Out_Error;
    if (Ehdr.e_ident[EI_DATA] != ELFDATA2LSB) goto Out_Error;
    if (Ehdr.e_version != EV_CURRENT) goto Out_Error;
    if (Ehdr.e_type != ET_EXEC) goto Out_Error;
    if (Ehdr.e_machine != EM_386) goto Out_Error;
    if (Ehdr.e_phnum == 0) goto Out_Error;
    if (Ehdr.e_phentsize < sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

    /* Determine reference bases from Info (computed by GetExecutableInfo_ELF) */
    CodeRef = Info->CodeBase;
    DataRef = Info->DataBase;

    /* First pass: compute final code/data span for entry validation */
    for (i = 0; i < (U32)Ehdr.e_phnum; ++i) {
        U32 phoff_i, vend;
        if (!Add32Overflow(Ehdr.e_phoff, i * (U32)Ehdr.e_phentsize, &phoff_i)) goto Out_Error;

        FileOperation.NumBytes = phoff_i;
        if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

        FileOperation.Buffer = (LPVOID)&Phdr;
        FileOperation.NumBytes = sizeof(EXOS_ELF32_PHDR);
        if (ReadFile(&FileOperation) != sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

        if (Phdr.p_type != PT_LOAD) continue;

        if (!Add32Overflow(Phdr.p_vaddr, Phdr.p_memsz, &vend)) goto Out_Error;

        if (ELFIsCode(Phdr.p_flags)) {
            HasCode = TRUE;
            if (Phdr.p_vaddr < CodeMin) CodeMin = Phdr.p_vaddr;
            if (vend > CodeMax) CodeMax = vend;
        } else {
            if (Phdr.p_vaddr < DataMin) DataMin = Phdr.p_vaddr;
            if (vend > DataMax) DataMax = vend;
        }
    }
    if (!HasCode) goto Out_Error;

    /* Second pass: load segments */
    for (i = 0; i < (U32)Ehdr.e_phnum; ++i) {
        U32 phoff_i;
        LINEAR Base = 0;
        U32 Ref = 0;
        U32 Dest;
        U32 CopySize;
        U32 ZeroSize;

        if (!Add32Overflow(Ehdr.e_phoff, i * (U32)Ehdr.e_phentsize, &phoff_i)) goto Out_Error;

        FileOperation.NumBytes = phoff_i;
        if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

        FileOperation.Buffer = (LPVOID)&Phdr;
        FileOperation.NumBytes = sizeof(EXOS_ELF32_PHDR);
        if (ReadFile(&FileOperation) != sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

        if (Phdr.p_type != PT_LOAD) continue;

        if (ELFIsCode(Phdr.p_flags)) {
            Base = CodeBase;
            Ref = CodeRef;
        } else {
            Base = DataBase;
            Ref = DataRef;
        }

        /* Compute destination address */
        if (Phdr.p_vaddr < Ref) goto Out_Error; /* malformed layout */
        Dest = (U32)Base + (Phdr.p_vaddr - Ref);

        /* Copy file-backed bytes */
        CopySize = Phdr.p_filesz;
        ZeroSize = (Phdr.p_memsz > Phdr.p_filesz) ? (Phdr.p_memsz - Phdr.p_filesz) : 0;

        if (CopySize > 0) {
            /* Validate file range */
            U32 fend;
            if (!Add32Overflow(Phdr.p_offset, CopySize, &fend)) goto Out_Error;
            if (fend > FileSize) goto Out_Error;

            FileOperation.NumBytes = Phdr.p_offset;
            if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

            FileOperation.Buffer = (LPVOID)(Dest);
            FileOperation.NumBytes = CopySize;
            if (ReadFile(&FileOperation) != CopySize) goto Out_Error;
        }

        /* Zero-fill BSS tail, if any */
        if (ZeroSize > 0) {
            MemorySet((LPVOID)(Dest + CopySize), 0, ZeroSize);
        }
    }

    /* Fix up the effective entry point */
    if (Ehdr.e_entry >= CodeMin && Ehdr.e_entry < CodeMax) {
        Info->EntryPoint = (U32)CodeBase + (Ehdr.e_entry - CodeRef);
    } else if (Ehdr.e_entry >= DataMin && Ehdr.e_entry < DataMax) {
        Info->EntryPoint = (U32)DataBase + (Ehdr.e_entry - DataRef);
    } else {
        goto Out_Error;
    }

    DEBUG(TEXT("[LoadExecutable_ELF] Exit (success)"));
    return TRUE;

Out_Error:
    DEBUG(TEXT("[LoadExecutable_ELF] Exit (error)"));
    return FALSE;
}
