
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

typedef struct __attribute__((packed)) tag_EXOS_ELF64_EHDR {
    U8 e_ident[EI_NIDENT];
    U16 e_type;
    U16 e_machine;
    U32 e_version;
    U64 e_entry;
    U64 e_phoff;
    U64 e_shoff;
    U32 e_flags;
    U16 e_ehsize;
    U16 e_phentsize;
    U16 e_phnum;
    U16 e_shentsize;
    U16 e_shnum;
    U16 e_shstrndx;
} EXOS_ELF64_EHDR;

typedef struct __attribute__((packed)) tag_EXOS_ELF64_PHDR {
    U32 p_type;
    U32 p_flags;
    U64 p_offset;
    U64 p_vaddr;
    U64 p_paddr;
    U64 p_filesz;
    U64 p_memsz;
    U64 p_align;
} EXOS_ELF64_PHDR;

/************************************************************************/
// Local helpers

static U32 ELFMakeSig(const U8 ident[EI_NIDENT]) {
    return ((U32)ident[0]) | ((U32)ident[1] << 8) | ((U32)ident[2] << 16) | ((U32)ident[3] << 24);
}

static BOOL ELFIsCode(U32 flags) { return (flags & PF_X) != 0; }
static BOOL ELFIsData(U32 flags) { return (flags & PF_W) != 0 || ((flags & PF_X) == 0); }

/* Safe register-sized range addition: returns FALSE on overflow */
static BOOL AddUIntOverflow(UINT a, UINT b, UINT* out) {
    UINT r = a + b;
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
    EXOS_ELF32_EHDR Ehdr32;
    EXOS_ELF32_PHDR Phdr32;
    EXOS_ELF64_EHDR Ehdr64;
    EXOS_ELF64_PHDR Phdr64;
    U32 FileSize;
    U32 Sig;
    U32 i;
    U8 Ident[EI_NIDENT];
    U8 Class;

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
    if (FileSize < EI_NIDENT) goto Out_Error;

    /* Read ELF identification */
    FileOperation.NumBytes = 0;
    if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

    FileOperation.Buffer = (LPVOID)Ident;
    FileOperation.NumBytes = EI_NIDENT;
    if (ReadFile(&FileOperation) != EI_NIDENT) goto Out_Error;

    Sig = ELFMakeSig(Ident);
    if (Sig != ELF_SIGNATURE) goto Out_Error;

    Class = Ident[EI_CLASS];

    if (Class == ELFCLASS32) {
        UINT CodeMin = MAX_UINT, CodeMax = 0;
        UINT DataMin = MAX_UINT, DataMax = 0;
        UINT BssMin = MAX_UINT, BssMax = 0;
        BOOL HasLoadable = FALSE;
        BOOL HasCode = FALSE;
        BOOL HasInterp = FALSE;
        U32 HeaderRemaining;

        if (FileSize < sizeof(EXOS_ELF32_EHDR)) goto Out_Error;

        MemoryCopy(Ehdr32.e_ident, Ident, EI_NIDENT);
        HeaderRemaining = sizeof(EXOS_ELF32_EHDR) - EI_NIDENT;
        FileOperation.Buffer = ((U8*)&Ehdr32) + EI_NIDENT;
        FileOperation.NumBytes = HeaderRemaining;
        if (ReadFile(&FileOperation) != HeaderRemaining) goto Out_Error;

        if (Ehdr32.e_ident[EI_DATA] != ELFDATA2LSB) goto Out_Error;
        if (Ehdr32.e_version != EV_CURRENT) goto Out_Error;
        if (Ehdr32.e_type != ET_EXEC) goto Out_Error;
        if (Ehdr32.e_machine != EM_386) goto Out_Error;

        if (Ehdr32.e_phnum == 0) goto Out_Error;
        if (Ehdr32.e_phentsize < sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

        {
            UINT pht_end;
            if (!AddUIntOverflow((UINT)Ehdr32.e_phoff, (UINT)Ehdr32.e_phnum * (UINT)Ehdr32.e_phentsize, &pht_end)) goto Out_Error;
            if (pht_end > FileSize) goto Out_Error;
        }

        for (i = 0; i < (U32)Ehdr32.e_phnum; ++i) {
            UINT phoff_i;
            UINT vend;
            BOOL IsCode;
            BOOL IsData;

            if (!AddUIntOverflow((UINT)Ehdr32.e_phoff, (UINT)i * (UINT)Ehdr32.e_phentsize, &phoff_i)) goto Out_Error;
            if (phoff_i > 0xFFFFFFFFU) goto Out_Error;

            FileOperation.NumBytes = (U32)phoff_i;
            if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

            FileOperation.Buffer = (LPVOID)&Phdr32;
            FileOperation.NumBytes = sizeof(EXOS_ELF32_PHDR);
            if (ReadFile(&FileOperation) != sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

            if (Phdr32.p_type == PT_INTERP) {
                HasInterp = TRUE;
            }
            if (Phdr32.p_type != PT_LOAD) continue;

            HasLoadable = TRUE;

            if (!AddUIntOverflow((UINT)Phdr32.p_vaddr, (UINT)Phdr32.p_memsz, &vend)) goto Out_Error;

            {
                UINT fend;
                if (!AddUIntOverflow((UINT)Phdr32.p_offset, (UINT)Phdr32.p_filesz, &fend)) goto Out_Error;
                if (fend > FileSize) goto Out_Error;
            }

            IsCode = ELFIsCode(Phdr32.p_flags);
            IsData = ELFIsData(Phdr32.p_flags);

            if (IsCode) {
                HasCode = TRUE;
                if ((UINT)Phdr32.p_vaddr < CodeMin) CodeMin = (UINT)Phdr32.p_vaddr;
                if (vend > CodeMax) CodeMax = vend;
            } else if (IsData) {
                if ((UINT)Phdr32.p_vaddr < DataMin) DataMin = (UINT)Phdr32.p_vaddr;
                if (vend > DataMax) DataMax = vend;
            } else {
                if ((UINT)Phdr32.p_vaddr < DataMin) DataMin = (UINT)Phdr32.p_vaddr;
                if (vend > DataMax) DataMax = vend;
            }

            if (Phdr32.p_memsz > Phdr32.p_filesz) {
                UINT bss_start, bss_end;
                if (!AddUIntOverflow((UINT)Phdr32.p_vaddr, (UINT)Phdr32.p_filesz, &bss_start)) goto Out_Error;
                if (!AddUIntOverflow((UINT)Phdr32.p_vaddr, (UINT)Phdr32.p_memsz, &bss_end)) goto Out_Error;
                if (bss_start < BssMin) BssMin = bss_start;
                if (bss_end > BssMax) BssMax = bss_end;
            }
        }

        if (!HasLoadable) goto Out_Error;
        if (!HasCode) goto Out_Error;
        if (HasInterp) goto Out_Error;

        Info->EntryPoint = (UINT)Ehdr32.e_entry;

        if (CodeMin != MAX_UINT && CodeMax > CodeMin) {
            Info->CodeBase = CodeMin;
            Info->CodeSize = CodeMax - CodeMin;
        } else {
            Info->CodeBase = 0;
            Info->CodeSize = 0;
        }

        if (DataMin != MAX_UINT && DataMax > DataMin) {
            Info->DataBase = DataMin;
            Info->DataSize = DataMax - DataMin;
        } else {
            Info->DataBase = 0;
            Info->DataSize = 0;
        }

        if (BssMin != MAX_UINT && BssMax > BssMin) {
            Info->BssBase = BssMin;
            Info->BssSize = BssMax - BssMin;
        } else {
            Info->BssBase = 0;
            Info->BssSize = 0;
        }

        Info->StackMinimum = 0;
        Info->StackRequested = 0;
        Info->HeapMinimum = 0;
        Info->HeapRequested = 0;

        DEBUG(TEXT("Exiting GetExecutableInfo_ELF (success)"));
        return TRUE;

#if defined(__EXOS_ARCH_X86_64__)
    } else if (Class == ELFCLASS64) {
        UINT CodeMin = MAX_UINT, CodeMax = 0;
        UINT DataMin = MAX_UINT, DataMax = 0;
        UINT BssMin = MAX_UINT, BssMax = 0;
        BOOL HasLoadable = FALSE;
        BOOL HasCode = FALSE;
        BOOL HasInterp = FALSE;
        U32 HeaderRemaining;

        if (FileSize < sizeof(EXOS_ELF64_EHDR)) goto Out_Error;

        MemoryCopy(Ehdr64.e_ident, Ident, EI_NIDENT);
        HeaderRemaining = sizeof(EXOS_ELF64_EHDR) - EI_NIDENT;
        FileOperation.Buffer = ((U8*)&Ehdr64) + EI_NIDENT;
        FileOperation.NumBytes = HeaderRemaining;
        if (ReadFile(&FileOperation) != HeaderRemaining) goto Out_Error;

        if (Ehdr64.e_ident[EI_DATA] != ELFDATA2LSB) goto Out_Error;
        if (Ehdr64.e_version != EV_CURRENT) goto Out_Error;
        if (Ehdr64.e_type != ET_EXEC) goto Out_Error;
        if (Ehdr64.e_machine != EM_X86_64) goto Out_Error;

        if (Ehdr64.e_phnum == 0) goto Out_Error;
        if (Ehdr64.e_phentsize < sizeof(EXOS_ELF64_PHDR)) goto Out_Error;

        {
            UINT pht_end;
            if (!AddUIntOverflow((UINT)Ehdr64.e_phoff, (UINT)Ehdr64.e_phnum * (UINT)Ehdr64.e_phentsize, &pht_end)) goto Out_Error;
            if (pht_end > FileSize) goto Out_Error;
        }

        for (i = 0; i < (U32)Ehdr64.e_phnum; ++i) {
            UINT phoff_i;
            UINT vend;
            BOOL IsCode;
            BOOL IsData;

            if (!AddUIntOverflow((UINT)Ehdr64.e_phoff, (UINT)i * (UINT)Ehdr64.e_phentsize, &phoff_i)) goto Out_Error;
            if (phoff_i > 0xFFFFFFFFU) goto Out_Error;

            FileOperation.NumBytes = (U32)phoff_i;
            if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

            FileOperation.Buffer = (LPVOID)&Phdr64;
            FileOperation.NumBytes = sizeof(EXOS_ELF64_PHDR);
            if (ReadFile(&FileOperation) != sizeof(EXOS_ELF64_PHDR)) goto Out_Error;

            if (Phdr64.p_type == PT_INTERP) {
                HasInterp = TRUE;
            }
            if (Phdr64.p_type != PT_LOAD) continue;

            HasLoadable = TRUE;

            if (!AddUIntOverflow((UINT)Phdr64.p_vaddr, (UINT)Phdr64.p_memsz, &vend)) goto Out_Error;

            {
                UINT fend;
                if (!AddUIntOverflow((UINT)Phdr64.p_offset, (UINT)Phdr64.p_filesz, &fend)) goto Out_Error;
                if (fend > FileSize) goto Out_Error;
            }

            IsCode = ELFIsCode(Phdr64.p_flags);
            IsData = ELFIsData(Phdr64.p_flags);

            if (IsCode) {
                HasCode = TRUE;
                if ((UINT)Phdr64.p_vaddr < CodeMin) CodeMin = (UINT)Phdr64.p_vaddr;
                if (vend > CodeMax) CodeMax = vend;
            } else if (IsData) {
                if ((UINT)Phdr64.p_vaddr < DataMin) DataMin = (UINT)Phdr64.p_vaddr;
                if (vend > DataMax) DataMax = vend;
            } else {
                if ((UINT)Phdr64.p_vaddr < DataMin) DataMin = (UINT)Phdr64.p_vaddr;
                if (vend > DataMax) DataMax = vend;
            }

            if (Phdr64.p_memsz > Phdr64.p_filesz) {
                UINT bss_start, bss_end;
                if (!AddUIntOverflow((UINT)Phdr64.p_vaddr, (UINT)Phdr64.p_filesz, &bss_start)) goto Out_Error;
                if (!AddUIntOverflow((UINT)Phdr64.p_vaddr, (UINT)Phdr64.p_memsz, &bss_end)) goto Out_Error;
                if (bss_start < BssMin) BssMin = bss_start;
                if (bss_end > BssMax) BssMax = bss_end;
            }
        }

        if (!HasLoadable) goto Out_Error;
        if (!HasCode) goto Out_Error;
        if (HasInterp) goto Out_Error;

        Info->EntryPoint = (UINT)Ehdr64.e_entry;

        if (CodeMin != MAX_UINT && CodeMax > CodeMin) {
            Info->CodeBase = CodeMin;
            Info->CodeSize = CodeMax - CodeMin;
        } else {
            Info->CodeBase = 0;
            Info->CodeSize = 0;
        }

        if (DataMin != MAX_UINT && DataMax > DataMin) {
            Info->DataBase = DataMin;
            Info->DataSize = DataMax - DataMin;
        } else {
            Info->DataBase = 0;
            Info->DataSize = 0;
        }

        if (BssMin != MAX_UINT && BssMax > BssMin) {
            Info->BssBase = BssMin;
            Info->BssSize = BssMax - BssMin;
        } else {
            Info->BssBase = 0;
            Info->BssSize = 0;
        }

        Info->StackMinimum = 0;
        Info->StackRequested = 0;
        Info->HeapMinimum = 0;
        Info->HeapRequested = 0;

        DEBUG(TEXT("Exiting GetExecutableInfo_ELF (success)"));
        return TRUE;
#endif

    }

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
    EXOS_ELF32_EHDR Ehdr32;
    EXOS_ELF32_PHDR Phdr32;
#if defined(__EXOS_ARCH_X86_64__)
    EXOS_ELF64_EHDR Ehdr64;
    EXOS_ELF64_PHDR Phdr64;
#endif
    U32 FileSize;
    U8 Ident[EI_NIDENT];
    U8 Class;
    U32 i;

    UINT CodeRef = 0, DataRef = 0;
    UINT CodeMin = MAX_UINT, CodeMax = 0;
    UINT DataMin = MAX_UINT, DataMax = 0;
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
    if (FileSize < EI_NIDENT) goto Out_Error;

    /* Read ELF identification */
    FileOperation.NumBytes = 0;
    if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

    FileOperation.Buffer = (LPVOID)Ident;
    FileOperation.NumBytes = EI_NIDENT;
    if (ReadFile(&FileOperation) != EI_NIDENT) goto Out_Error;

    if (ELFMakeSig(Ident) != ELF_SIGNATURE) goto Out_Error;

    Class = Ident[EI_CLASS];

    if (Class == ELFCLASS32) {
        U32 HeaderRemaining;

        if (FileSize < sizeof(EXOS_ELF32_EHDR)) goto Out_Error;

        MemoryCopy(Ehdr32.e_ident, Ident, EI_NIDENT);
        HeaderRemaining = sizeof(EXOS_ELF32_EHDR) - EI_NIDENT;
        FileOperation.Buffer = ((U8*)&Ehdr32) + EI_NIDENT;
        FileOperation.NumBytes = HeaderRemaining;
        if (ReadFile(&FileOperation) != HeaderRemaining) goto Out_Error;

        if (Ehdr32.e_ident[EI_DATA] != ELFDATA2LSB) goto Out_Error;
        if (Ehdr32.e_version != EV_CURRENT) goto Out_Error;
        if (Ehdr32.e_type != ET_EXEC) goto Out_Error;
        if (Ehdr32.e_machine != EM_386) goto Out_Error;
        if (Ehdr32.e_phnum == 0) goto Out_Error;
        if (Ehdr32.e_phentsize < sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

        CodeRef = Info->CodeBase;
        DataRef = Info->DataBase;

        CodeMin = MAX_UINT;
        CodeMax = 0;
        DataMin = MAX_UINT;
        DataMax = 0;
        HasCode = FALSE;

        for (i = 0; i < (U32)Ehdr32.e_phnum; ++i) {
            UINT phoff_i;
            UINT vend;

            if (!AddUIntOverflow((UINT)Ehdr32.e_phoff, (UINT)i * (UINT)Ehdr32.e_phentsize, &phoff_i)) goto Out_Error;
            if (phoff_i > 0xFFFFFFFFU) goto Out_Error;

            FileOperation.NumBytes = (U32)phoff_i;
            if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

            FileOperation.Buffer = (LPVOID)&Phdr32;
            FileOperation.NumBytes = sizeof(EXOS_ELF32_PHDR);
            if (ReadFile(&FileOperation) != sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

            if (Phdr32.p_type != PT_LOAD) continue;

            if (!AddUIntOverflow((UINT)Phdr32.p_vaddr, (UINT)Phdr32.p_memsz, &vend)) goto Out_Error;

            if (ELFIsCode(Phdr32.p_flags)) {
                HasCode = TRUE;
                if ((UINT)Phdr32.p_vaddr < CodeMin) CodeMin = (UINT)Phdr32.p_vaddr;
                if (vend > CodeMax) CodeMax = vend;
            } else {
                if ((UINT)Phdr32.p_vaddr < DataMin) DataMin = (UINT)Phdr32.p_vaddr;
                if (vend > DataMax) DataMax = vend;
            }
        }
        if (!HasCode) goto Out_Error;

        for (i = 0; i < (U32)Ehdr32.e_phnum; ++i) {
            UINT phoff_i;
            LINEAR Base = 0;
            UINT Ref = 0;
            LINEAR Dest;
            U32 CopySize;
            U32 ZeroSize;

            if (!AddUIntOverflow((UINT)Ehdr32.e_phoff, (UINT)i * (UINT)Ehdr32.e_phentsize, &phoff_i)) goto Out_Error;
            if (phoff_i > 0xFFFFFFFFU) goto Out_Error;

            FileOperation.NumBytes = (U32)phoff_i;
            if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

            FileOperation.Buffer = (LPVOID)&Phdr32;
            FileOperation.NumBytes = sizeof(EXOS_ELF32_PHDR);
            if (ReadFile(&FileOperation) != sizeof(EXOS_ELF32_PHDR)) goto Out_Error;

            if (Phdr32.p_type != PT_LOAD) continue;

            if (ELFIsCode(Phdr32.p_flags)) {
                Base = CodeBase;
                Ref = CodeRef;
            } else {
                Base = DataBase;
                Ref = DataRef;
            }

            if ((UINT)Phdr32.p_vaddr < Ref) goto Out_Error;
            Dest = Base + ((UINT)Phdr32.p_vaddr - Ref);

            CopySize = Phdr32.p_filesz;
            ZeroSize = (Phdr32.p_memsz > Phdr32.p_filesz) ? (Phdr32.p_memsz - Phdr32.p_filesz) : 0;

            if (CopySize > 0) {
                UINT fend;
                if (!AddUIntOverflow((UINT)Phdr32.p_offset, (UINT)CopySize, &fend)) goto Out_Error;
                if (fend > FileSize) goto Out_Error;

                FileOperation.NumBytes = Phdr32.p_offset;
                if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

                FileOperation.Buffer = (LPVOID)Dest;
                FileOperation.NumBytes = CopySize;
                if (ReadFile(&FileOperation) != CopySize) goto Out_Error;
            }

            if (ZeroSize > 0) {
                MemorySet((LPVOID)(Dest + CopySize), 0, ZeroSize);
            }
        }

        if ((UINT)Ehdr32.e_entry >= CodeMin && (UINT)Ehdr32.e_entry < CodeMax) {
            Info->EntryPoint = (UINT)CodeBase + ((UINT)Ehdr32.e_entry - CodeRef);
        } else if ((UINT)Ehdr32.e_entry >= DataMin && (UINT)Ehdr32.e_entry < DataMax) {
            Info->EntryPoint = (UINT)DataBase + ((UINT)Ehdr32.e_entry - DataRef);
        } else {
            goto Out_Error;
        }

        DEBUG(TEXT("[LoadExecutable_ELF] Exit (success)"));
        return TRUE;
#if defined(__EXOS_ARCH_X86_64__)
    } else if (Class == ELFCLASS64) {
        U32 HeaderRemaining;

        if (FileSize < sizeof(EXOS_ELF64_EHDR)) goto Out_Error;

        MemoryCopy(Ehdr64.e_ident, Ident, EI_NIDENT);
        HeaderRemaining = sizeof(EXOS_ELF64_EHDR) - EI_NIDENT;
        FileOperation.Buffer = ((U8*)&Ehdr64) + EI_NIDENT;
        FileOperation.NumBytes = HeaderRemaining;
        if (ReadFile(&FileOperation) != HeaderRemaining) goto Out_Error;

        if (Ehdr64.e_ident[EI_DATA] != ELFDATA2LSB) goto Out_Error;
        if (Ehdr64.e_version != EV_CURRENT) goto Out_Error;
        if (Ehdr64.e_type != ET_EXEC) goto Out_Error;
        if (Ehdr64.e_machine != EM_X86_64) goto Out_Error;
        if (Ehdr64.e_phnum == 0) goto Out_Error;
        if (Ehdr64.e_phentsize < sizeof(EXOS_ELF64_PHDR)) goto Out_Error;

        CodeRef = Info->CodeBase;
        DataRef = Info->DataBase;

        CodeMin = MAX_UINT;
        CodeMax = 0;
        DataMin = MAX_UINT;
        DataMax = 0;
        HasCode = FALSE;

        for (i = 0; i < (U32)Ehdr64.e_phnum; ++i) {
            UINT phoff_i;
            UINT vend;

            if (!AddUIntOverflow((UINT)Ehdr64.e_phoff, (UINT)i * (UINT)Ehdr64.e_phentsize, &phoff_i)) goto Out_Error;
            if (phoff_i > 0xFFFFFFFFU) goto Out_Error;

            FileOperation.NumBytes = (U32)phoff_i;
            if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

            FileOperation.Buffer = (LPVOID)&Phdr64;
            FileOperation.NumBytes = sizeof(EXOS_ELF64_PHDR);
            if (ReadFile(&FileOperation) != sizeof(EXOS_ELF64_PHDR)) goto Out_Error;

            if (Phdr64.p_type != PT_LOAD) continue;

            if (!AddUIntOverflow((UINT)Phdr64.p_vaddr, (UINT)Phdr64.p_memsz, &vend)) goto Out_Error;

            if (ELFIsCode(Phdr64.p_flags)) {
                HasCode = TRUE;
                if ((UINT)Phdr64.p_vaddr < CodeMin) CodeMin = (UINT)Phdr64.p_vaddr;
                if (vend > CodeMax) CodeMax = vend;
            } else {
                if ((UINT)Phdr64.p_vaddr < DataMin) DataMin = (UINT)Phdr64.p_vaddr;
                if (vend > DataMax) DataMax = vend;
            }
        }
        if (!HasCode) goto Out_Error;

        for (i = 0; i < (U32)Ehdr64.e_phnum; ++i) {
            UINT phoff_i;
            LINEAR Base = 0;
            UINT Ref = 0;
            LINEAR Dest;
            U32 CopySize;
            UINT ZeroSize;

            if (!AddUIntOverflow((UINT)Ehdr64.e_phoff, (UINT)i * (UINT)Ehdr64.e_phentsize, &phoff_i)) goto Out_Error;
            if (phoff_i > 0xFFFFFFFFU) goto Out_Error;

            FileOperation.NumBytes = (U32)phoff_i;
            if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

            FileOperation.Buffer = (LPVOID)&Phdr64;
            FileOperation.NumBytes = sizeof(EXOS_ELF64_PHDR);
            if (ReadFile(&FileOperation) != sizeof(EXOS_ELF64_PHDR)) goto Out_Error;

            if (Phdr64.p_type != PT_LOAD) continue;

            if (ELFIsCode(Phdr64.p_flags)) {
                Base = CodeBase;
                Ref = CodeRef;
            } else {
                Base = DataBase;
                Ref = DataRef;
            }

            if ((UINT)Phdr64.p_vaddr < Ref) goto Out_Error;
            Dest = Base + ((UINT)Phdr64.p_vaddr - Ref);

            if (Phdr64.p_filesz > 0xFFFFFFFFU) goto Out_Error;
            if (Phdr64.p_offset > 0xFFFFFFFFU) goto Out_Error;

            CopySize = (U32)Phdr64.p_filesz;
            ZeroSize = (UINT)((Phdr64.p_memsz > Phdr64.p_filesz) ? (Phdr64.p_memsz - Phdr64.p_filesz) : 0);

            if (CopySize > 0) {
                UINT fend;
                if (!AddUIntOverflow((UINT)Phdr64.p_offset, (UINT)CopySize, &fend)) goto Out_Error;
                if (fend > FileSize) goto Out_Error;

                FileOperation.NumBytes = (U32)Phdr64.p_offset;
                if (SetFilePosition(&FileOperation) != DF_ERROR_SUCCESS) goto Out_Error;

                FileOperation.Buffer = (LPVOID)Dest;
                FileOperation.NumBytes = CopySize;
                if (ReadFile(&FileOperation) != CopySize) goto Out_Error;
            }

            if (ZeroSize > 0) {
                MemorySet((LPVOID)(Dest + (UINT)CopySize), 0, ZeroSize);
            }
        }

        if ((UINT)Ehdr64.e_entry >= CodeMin && (UINT)Ehdr64.e_entry < CodeMax) {
            Info->EntryPoint = (UINT)CodeBase + ((UINT)Ehdr64.e_entry - CodeRef);
        } else if ((UINT)Ehdr64.e_entry >= DataMin && (UINT)Ehdr64.e_entry < DataMax) {
            Info->EntryPoint = (UINT)DataBase + ((UINT)Ehdr64.e_entry - DataRef);
        } else {
            goto Out_Error;
        }

        DEBUG(TEXT("[LoadExecutable_ELF] Exit (success)"));
        return TRUE;
#endif
    }

Out_Error:
    DEBUG(TEXT("[LoadExecutable_ELF] Exit (error)"));
    return FALSE;
}
