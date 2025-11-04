
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


    x86-64 memory internal interfaces

\************************************************************************/


#ifndef X86_64_MEMORY_INTERNAL_H_INCLUDED
#define X86_64_MEMORY_INTERNAL_H_INCLUDED

#include "Memory.h"
#include "Console.h"
#include "CoreString.h"
#include "Kernel.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "Log.h"
#include "Stack.h"
#include "System.h"
#include "Text.h"
#include "arch/x86-64/x86-64.h"
#include "arch/x86-64/x86-64-Log.h"

#ifndef EXOS_X86_64_FAST_VMM
#define EXOS_X86_64_FAST_VMM 1
#endif

extern BOOL G_RegionDescriptorsEnabled;
extern BOOL G_RegionDescriptorBootstrap;
extern LPMEMORY_REGION_DESCRIPTOR G_FreeRegionDescriptors;
extern UINT G_FreeRegionDescriptorCount;
extern UINT G_TotalRegionDescriptorCount;
extern UINT G_RegionDescriptorPages;

LPPROCESS ResolveCurrentAddressSpaceOwner(void);
void InitializeRegionDescriptorTracking(void);
LPMEMORY_REGION_DESCRIPTOR FindDescriptorForBase(LPPROCESS Process, LINEAR CanonicalBase);
LPMEMORY_REGION_DESCRIPTOR FindDescriptorCoveringAddress(LPPROCESS Process, LINEAR CanonicalBase);
void ExtendDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor, UINT AdditionalPages);
BOOL RegisterRegionDescriptor(LINEAR Base, UINT NumPages, PHYSICAL Target, U32 Flags);
void UpdateDescriptorsForFree(LINEAR Base, UINT SizeBytes);

#if EXOS_X86_64_FAST_VMM
void InitializeTransientDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor,
                                   LINEAR Base,
                                   UINT PageCount,
                                   PHYSICAL PhysicalBase,
                                   U32 Flags);
BOOL FastPopulateRegionFromDescriptor(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    PHYSICAL Target,
    U32 Flags,
    LPCSTR FunctionName,
    UINT* OutPagesProcessed);
BOOL FastReleaseRegionFromDescriptor(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    UINT* OutPagesProcessed);
BOOL ReleaseRegionWithFastWalker(LINEAR CanonicalBase, UINT NumPages);
#endif

BOOL FreeRegionLegacyInternal(LINEAR CanonicalBase, UINT NumPages, LINEAR OriginalBase, UINT Size);
UINT ComputePagesUntilAlignment(LINEAR Base, U64 SpanSize);
BOOL IsRegionFree(LINEAR Base, UINT Size);
void FreeEmptyPageTables(void);

#endif  // X86_64_MEMORY_INTERNAL_H_INCLUDED
