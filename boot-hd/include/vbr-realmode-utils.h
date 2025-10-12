
/************************************************************************\

    EXOS Bootloader
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


    Segment:Offset address conversion functions

\************************************************************************/

#ifndef VBR_REALMODE_UTILS_H_INCLUDED
#define VBR_REALMODE_UTILS_H_INCLUDED

/************************************************************************/
// Common low-memory layout used by the VBR payload

#ifndef PAYLOAD_ADDRESS
#error "PAYLOAD_ADDRESS is not defined"
#endif

#define ORIGIN PAYLOAD_ADDRESS
#define STACK_SIZE 0x1000
#define USABLE_RAM_START 0x1000
#define USABLE_RAM_END (ORIGIN - STACK_SIZE)
#define USABLE_RAM_SIZE (USABLE_RAM_END - USABLE_RAM_START)

#define SECTORSIZE 512

#define KERNEL_LINEAR_LOAD_ADDRESS 0x00200000u

/************************************************************************/
// Inline helpers for segment arithmetic

// Pack segment and offset into a single U32 (seg:ofs as 0xSSSSOOOO)
static inline U32 PackSegOfs(U16 Seg, U16 Ofs) {
    return ((U32)Seg << 16) | (U32)Ofs;
}

/************************************************************************/

// Convert segment:offset to linear address
static inline U32 SegOfsToLinear(U16 Seg, U16 Ofs) {
    return ((U32)Seg << 4) | (U32)Ofs;
}

/************************************************************************/

// Build seg:ofs from a linear pointer. Aligns segment down to 16 bytes.
// For linear 0x20000, you get 0x20000000 which represents 0x2000:0x0000
static inline U32 LinearToSegOfs(const void* Ptr) {
    U32 Lin = (U32)Ptr;
    U16 Seg = (U16)(Lin >> 4);
    U16 Ofs = (U16)(Lin & 0xF);
    return PackSegOfs(Seg, Ofs);
}

/************************************************************************/

static inline void Hang(void) {
    do {
        __asm__ __volatile__(
            "1:\n\t"
            "cli\n\t"
            "hlt\n\t"
            "jmp 1b\n\t"
            :
            :
            : "memory");
    } while (0);
}

/************************************************************************/

#define GetCS(var) __asm__ volatile("movw %%cs, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")
#define GetDS(var) __asm__ volatile("movw %%ds, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")
#define GetSS(var) __asm__ volatile("movw %%ss, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")
#define GetSP(var) __asm__ volatile("movw %%sp, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")

/************************************************************************/
// Shared helpers exposed by the common payload code

extern STR TempString[128];

void BootDebugPrint(LPCSTR Str);
void BootVerbosePrint(LPCSTR Str);
void BootErrorPrint(LPCSTR Str);
const char* BootGetFileName(const char* Path);

/************************************************************************/
// Functions in vbr-payload-a.asm

extern U32 BiosReadSectors(U32 Drive, U32 Lba, U32 Count, U32 Dest);
extern void MemorySet(LPVOID Base, U32 What, U32 Size);
extern void MemoryCopy(LPVOID Destination, LPCVOID Source, U32 Size);
extern void UnrealMemoryCopy(U32 DestinationLinear, U32 SourceLinear, U32 Size);
extern U32 BiosGetMemoryMap(U32 Buffer, U32 MaxEntries);
extern U32 VESAGetModeInfo(U16 Mode, U32 Buffer);
extern U32 VESASetMode(U16 Mode);
extern void SetPixel24(U32 x, U32 y, U32 color, U32 framebuffer);
extern void EnableA20(void);

extern void __attribute__((noreturn)) StubJumpToImage(
    U32 GDTR,
    U32 PageStructurePA,
    U32 KernelEntryLo,
    U32 KernelEntryHi,
    U32 MultibootInfoPtr,
    U32 MultibootMagic);

#endif // VBR_REALMODE_UTILS_H_INCLUDED
