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


    Memory region descriptor tracking (portable)

\************************************************************************/

#ifndef MEMORY_DESCRIPTORS_H_INCLUDED
#define MEMORY_DESCRIPTORS_H_INCLUDED

#include "Memory.h"
#include "process/Process.h"

/************************************************************************/
// #defines

/************************************************************************/
// typedefs

/************************************************************************/
// inlines

/************************************************************************/
// external symbols

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
BOOL RegisterRegionDescriptor(LINEAR Base, UINT NumPages, PHYSICAL Target, U32 Flags, LPCSTR Tag);
void UpdateDescriptorsForFree(LINEAR Base, UINT SizeBytes);
void RefreshDescriptorGranularity(LPMEMORY_REGION_DESCRIPTOR Descriptor);
MEMORY_REGION_GRANULARITY ComputeDescriptorGranularity(LINEAR Base, UINT PageCount);
BOOL RegionTrackAlloc(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag);
BOOL RegionTrackFree(LINEAR Base, UINT Size);
BOOL RegionTrackResize(LINEAR Base, UINT OldSize, UINT NewSize, U32 Flags);

#endif  // MEMORY_DESCRIPTORS_H_INCLUDED
