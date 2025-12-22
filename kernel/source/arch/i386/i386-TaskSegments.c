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


    i386 Task segment initialization

\************************************************************************/

#include "arch/i386/i386.h"

#include "CoreString.h"
#include "Driver.h"
#include "Log.h"
#include "Memory.h"
#include "Text.h"
#include "User.h"

/************************************************************************/

#define TASK_SEGMENTS_VER_MAJOR 1
#define TASK_SEGMENTS_VER_MINOR 0

static UINT TaskSegmentsDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION TaskSegmentsDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_OTHER,
    .VersionMajor = TASK_SEGMENTS_VER_MAJOR,
    .VersionMinor = TASK_SEGMENTS_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "TaskSegments",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = TaskSegmentsDriverCommands};

/***************************************************************************/

void InitializeTaskSegments(void) {
    DEBUG(TEXT("[InitializeTaskSegments] Enter"));

    U32 TssSize = sizeof(TASK_STATE_SEGMENT);

    Kernel_i386.TSS = (LPTASK_STATE_SEGMENT)AllocKernelRegion(
        0, TssSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("TSS"));

    if (Kernel_i386.TSS == NULL) {
        ERROR(TEXT("[InitializeTaskSegments] AllocRegion for TSS failed"));
        DO_THE_SLEEPING_BEAUTY;
    }

    MemorySet(Kernel_i386.TSS, 0, TssSize);

    LPTSS_DESCRIPTOR Desc = (LPTSS_DESCRIPTOR)(Kernel_i386.GDT + GDT_TSS_INDEX);
    Desc->Type = GATE_TYPE_386_TSS_AVAIL;
    Desc->Privilege = GDT_PRIVILEGE_USER;
    Desc->Present = 1;
    Desc->Granularity = GDT_GRANULAR_1B;
    SetTSSDescriptorBase(Desc, (U32)Kernel_i386.TSS);
    SetTSSDescriptorLimit(Desc, sizeof(TASK_STATE_SEGMENT) - 1);

    DEBUG(TEXT("[InitializeTaskSegments] TSS = %p"), Kernel_i386.TSS);
    DEBUG(TEXT("[InitializeTaskSegments] Loading task register"));

    LoadInitialTaskRegister(SELECTOR_TSS);

    DEBUG(TEXT("[InitializeTaskSegments] Exit"));
}

/***************************************************************************/

void SetTSSDescriptorBase(LPTSS_DESCRIPTOR This, U32 Base) {
    SetSegmentDescriptorBase((LPSEGMENT_DESCRIPTOR)This, Base);
}

/***************************************************************************/

void SetTSSDescriptorLimit(LPTSS_DESCRIPTOR This, U32 Limit) {
    SetSegmentDescriptorLimit((LPSEGMENT_DESCRIPTOR)This, Limit);
}

/***************************************************************************/

/**
 * @brief Driver command handler for task segment initialization.
 */
static UINT TaskSegmentsDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((TaskSegmentsDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RET_SUCCESS;
            }

            InitializeTaskSegments();
            TaskSegmentsDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RET_SUCCESS;

        case DF_UNLOAD:
            if ((TaskSegmentsDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RET_SUCCESS;
            }

            TaskSegmentsDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RET_SUCCESS;

        case DF_GETVERSION:
            return MAKE_VERSION(TASK_SEGMENTS_VER_MAJOR, TASK_SEGMENTS_VER_MINOR);
    }

    return DF_RET_NOTIMPL;
}
