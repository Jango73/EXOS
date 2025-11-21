
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


    Stack operations

\************************************************************************/

#include "Base.h"
#include "Stack.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "process/Process.h"
#include "CoreString.h"

/************************************************************************/

/**
 * @brief Locate the active stack descriptor that contains the provided SP.
 * @param Task Task owning the potential stacks.
 * @param CurrentSP Linear stack pointer to classify.
 * @return Pointer to the matching STACK descriptor or NULL if none matches.
 */
static LPSTACK StackLocateActiveDescriptor(LPTASK Task, LINEAR CurrentSP) {
    if (Task == NULL) {
        return NULL;
    }

    if (Task->Arch.Stack.Base != 0 && Task->Arch.Stack.Size != 0) {
        LINEAR Base = Task->Arch.Stack.Base;
        LINEAR Top = Base + (LINEAR)Task->Arch.Stack.Size;
        if (CurrentSP >= Base && CurrentSP <= Top) {
            return &(Task->Arch.Stack);
        }
    }

    if (Task->Arch.SysStack.Base != 0 && Task->Arch.SysStack.Size != 0) {
        LINEAR Base = Task->Arch.SysStack.Base;
        LINEAR Top = Base + (LINEAR)Task->Arch.SysStack.Size;
        if (CurrentSP >= Base && CurrentSP <= Top) {
            return &(Task->Arch.SysStack);
        }
    }

#if defined(__EXOS_ARCH_X86_64__)
    if (Task->Arch.Ist1Stack.Base != 0 && Task->Arch.Ist1Stack.Size != 0) {
        LINEAR Base = Task->Arch.Ist1Stack.Base;
        LINEAR Top = Base + (LINEAR)Task->Arch.Ist1Stack.Size;
        if (CurrentSP >= Base && CurrentSP <= Top) {
            return &(Task->Arch.Ist1Stack);
        }
    }
#endif

    return NULL;
}

/************************************************************************/

#if defined(__EXOS_ARCH_I386__)
static inline SELECTOR StackReadCodeSegment(void) {
    U32 SegmentValue;

    GetCS(SegmentValue);

    return (SELECTOR)SegmentValue;
}

static inline UINT StackGetSavedPointer(LPTASK Task) {
    return Task->Arch.Context.Registers.ESP;
}
#else
static inline SELECTOR StackReadCodeSegment(void) {
    SELECTOR SegmentValue;

    __asm__ __volatile__("movw %%cs, %0" : "=r"(SegmentValue));

    return SegmentValue;
}

static inline UINT StackGetSavedPointer(LPTASK Task) {
    return (UINT)Task->Arch.Context.Registers.RSP;
}
#endif

/************************************************************************/

/**
 * @brief Copies stack content and adjusts EBP chain pointers.
 *
 * This function copies stack content from source to destination and walks
 * the frame chain to adjust all EBP values that point within the source stack.
 *
 * @param DestStackTop Top address of destination stack
 * @param SourceStackTop Top address of source stack
 * @param Size Number of bytes to copy
 * @param StartEBP Starting EBP value to begin frame chain adjustment
 * @return TRUE on success, FALSE if parameters are invalid or EBP is out of range
 */
BOOL CopyStackWithEBP(LINEAR DestStackTop, LINEAR SourceStackTop, UINT Size, LINEAR StartEBP) {
    if (!DestStackTop || !SourceStackTop || Size == 0) {
        return FALSE;
    }

    LINEAR SourceStackStart = SourceStackTop - Size;
    LINEAR DestStackStart = DestStackTop - Size;
    UINT Delta = (INT)(DestStackTop - SourceStackTop);

    // Copy stack content from source to destination
    MemoryCopy((void *)DestStackStart, (const void *)SourceStackStart, Size);

    // Walk the frame chain and adjust all EBP values
    LINEAR CurrentEbp = StartEBP;

    // Only adjust if current EBP is within source stack range
    if (CurrentEbp >= SourceStackStart && CurrentEbp < SourceStackTop) {
        LINEAR AdjustedCurrentEbp = CurrentEbp + (LINEAR)Delta;
        LINEAR WalkEbp = AdjustedCurrentEbp;

        while (WalkEbp >= DestStackStart && WalkEbp < DestStackTop) {
            LINEAR *Fp = (LINEAR *)(UINT)WalkEbp;
            LINEAR SavedEbp = Fp[0];

            // If saved EBP points into the source stack, adjust it
            if (SavedEbp >= SourceStackStart && SavedEbp < SourceStackTop) {
                LINEAR NewSavedEbp = SavedEbp + (LINEAR)Delta;
                Fp[0] = NewSavedEbp;
                WalkEbp = NewSavedEbp;  // Continue with adjusted value
            } else {
                break;  // End of chain or points outside our stack
            }
        }

        return TRUE;
    } else {
        return FALSE;
    }
}

/**
 * @brief Copies stack content using current EBP as starting point.
 *
 * Convenience wrapper around CopyStackWithEBP that uses the current
 * EBP register value as the starting point for frame chain adjustment.
 *
 * @param DestStackTop Top address of destination stack
 * @param SourceStackTop Top address of source stack
 * @param Size Number of bytes to copy
 * @return TRUE on success, FALSE on failure
 */
BOOL CopyStack(LINEAR DestStackTop, LINEAR SourceStackTop, UINT Size) {
#if defined(__EXOS_ARCH_I386__)
    LINEAR CurrentEbp;
    GetEBP(CurrentEbp);
    return CopyStackWithEBP(DestStackTop, SourceStackTop, Size, CurrentEbp);
#else
    LINEAR CurrentEbp;
    GetEBP(CurrentEbp);
    return CopyStackWithEBP(DestStackTop, SourceStackTop, Size, CurrentEbp);
#endif
}

/************************************************************************/

/**
 * @brief Copies stack content and switches ESP/EBP to the new stack.
 *
 * This function copies the stack content from source to destination,
 * adjusts frame pointers, and then switches the current ESP and EBP
 * registers to point to the corresponding locations in the destination stack.
 *
 * @param DestStackTop Top address of destination stack
 * @param SourceStackTop Top address of source stack
 * @param Size Number of bytes to copy and switch
 * @return TRUE if stack switch successful, FALSE if copy failed or ESP out of range
 */
BOOL SwitchStack(LINEAR DestStackTop, LINEAR SourceStackTop, UINT Size) {
    if (!CopyStack(DestStackTop, SourceStackTop, Size)) {
        return FALSE;
    }

    LINEAR SourceStackStart = SourceStackTop - Size;
    INT Delta = DestStackTop - SourceStackTop;

    // Get current ESP and EBP at the moment of switch
    LINEAR CurrentSP;
    LINEAR CurrentBP;

    GetESP(CurrentSP);
    GetEBP(CurrentBP);

    DEBUG(TEXT("[SwitchStack] Current ESP=%p, EBP=%p at switch time"), CurrentSP, CurrentBP);

    // Check if we're within the source stack range
    if (CurrentSP >= SourceStackStart && CurrentSP < SourceStackTop) {
        LINEAR NewSP = CurrentSP + Delta;
        LINEAR NewBP = CurrentBP + Delta;

        DEBUG(TEXT("[SwitchStack] Switching SP %p -> %p, BP %p -> %p"), CurrentSP, NewSP, CurrentBP, NewBP);

        // Switch SP and BP
#if defined(__EXOS_ARCH_I386__)
        __asm__ __volatile__(
            "mov %0, %%esp\n\t"
            "mov %1, %%ebp"
            :
            : "r"(NewSP), "r"(NewBP)
            : "memory");
#else
        __asm__ __volatile__(
            "mov %0, %%rsp\n\t"
            "mov %1, %%rbp"
            :
            : "r"(NewSP), "r"(NewBP)
            : "memory");
#endif

        return TRUE;
    }

    DEBUG(TEXT("[SwitchStack] SP %p not in source stack range [%p-%p]"), CurrentSP, SourceStackStart,
        SourceStackTop);

    return FALSE;
}

/************************************************************************/

/**
 * @brief Validates that the current task's ESP is within valid stack bounds.
 *
 * This function checks if the current ESP register value falls within the
 * expected stack range for the current task. For kernel tasks, it checks
 * against the normal stack. For user tasks, it checks the appropriate stack
 * based on current execution mode. Includes safety margin checking to detect
 * near-overflows.
 *
 * @return TRUE if stack is valid, FALSE if overflow or bounds violation detected
 */
BOOL CheckStack(void) {
    LPTASK CurrentTask;
    UINT CurrentESP;
    SELECTOR CurrentCS;
    UINT StackBase, StackTop;
    BOOL InKernelMode;

    CurrentTask = GetCurrentTask();

    if (CurrentTask == NULL) {
        return TRUE;
    }

    // Skip stack checking for main kernel task since ESP is not saved in context
    if (CurrentTask->Flags & TASK_CREATE_MAIN_KERNEL) {
        return TRUE;
    }

    CurrentCS = StackReadCodeSegment();
    InKernelMode = ((CurrentCS & SELECTOR_RPL_MASK) == 0);

    // Determine which ESP to check and which stack bounds to use
    if (CurrentTask->Process->Privilege == PRIVILEGE_KERNEL) {
        // Kernel tasks always use their normal stack
        CurrentESP = StackGetSavedPointer(CurrentTask);
        StackBase = CurrentTask->Arch.Stack.Base;
        StackTop = StackBase + CurrentTask->Arch.Stack.Size;
    } else if (InKernelMode) {
        // User task currently in kernel mode (via syscall/interrupt)
        // The hardware switches to ESP0 stack, which may not be the task's system stack
        // We cannot reliably validate the current ESP since it might be on a different kernel stack
        // Instead, we just verify the task has a valid system stack allocated
        if (CurrentTask->Arch.SysStack.Base == 0 || CurrentTask->Arch.SysStack.Size == 0) {
            ERROR(TEXT("[CheckStack] User task in kernel mode without system stack!"));
            ERROR(TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name, CurrentTask->Process->FileName);
            return FALSE;
        }
        // For userland tasks in kernel mode, skip ESP validation as the current ESP
        // might be on the TSS ESP0 stack or another kernel stack, not the task's system stack
        return TRUE;
    } else {
        // User task in user mode - check saved user stack ESP
        CurrentESP = StackGetSavedPointer(CurrentTask);
        StackBase = CurrentTask->Arch.Stack.Base;
        StackTop = StackBase + CurrentTask->Arch.Stack.Size;
    }

    if (CurrentESP < StackBase || CurrentESP > StackTop) {
        ERROR(TEXT("[CheckStack] ESP OUTSIDE STACK BOUNDS!"));
        ERROR(TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name, CurrentTask->Process->FileName);
        ERROR(TEXT("[CheckStack] ESP: %x"), CurrentESP);
        ERROR(TEXT("[CheckStack] StackBase: %x"), StackBase);
        ERROR(TEXT("[CheckStack] StackTop: %x"), StackTop);
        ERROR(TEXT("[CheckStack] InKernelMode: %u"), InKernelMode ? 1 : 0);

        if (CurrentESP < StackBase) {
            ERROR(TEXT("[CheckStack] ESP is %u bytes below stack base (severe underflow)"),
                StackBase - CurrentESP);
        } else {
            ERROR(TEXT("[CheckStack] ESP is %u bytes above stack top (overflow)"), CurrentESP - StackTop);
        }

        return FALSE;
    }

    if (CurrentESP <= (StackBase + STACK_SAFETY_MARGIN)) {
        ERROR(TEXT("[CheckStack] STACK OVERFLOW DETECTED!"));
        ERROR(TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name, CurrentTask->Process->FileName);
        ERROR(TEXT("[CheckStack] Func: %x"), CurrentTask ? CurrentTask->Function : 0);
        ERROR(TEXT("[CheckStack] ESP: %x"), CurrentESP);
        ERROR(TEXT("[CheckStack] StackBase: %x"), StackBase);
        ERROR(TEXT("[CheckStack] StackTop: %x"), StackTop);
        ERROR(TEXT("[CheckStack] InKernelMode: %u"), InKernelMode ? 1 : 0);
        ERROR(TEXT("[CheckStack] Safety margin violated by %u bytes"), (StackBase + STACK_SAFETY_MARGIN) - CurrentESP);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Compute the number of free bytes remaining on the current stack.
 * @return Free byte count between the stack base and the current SP.
 */
UINT GetCurrentStackFreeBytes(void) {
    LPTASK CurrentTask = GetCurrentTask();
    if (CurrentTask == NULL) {
        return MAX_UINT;
    }

    UINT RemainingBytes = 0;
    BOOL TaskValidated = FALSE;

    SAFE_USE_VALID_ID(CurrentTask, KOID_TASK) {
        TaskValidated = TRUE;

        LINEAR CurrentSP;
        GetESP(CurrentSP);

        LPSTACK ActiveStack = StackLocateActiveDescriptor(CurrentTask, CurrentSP);

        if (ActiveStack != NULL && ActiveStack->Base != 0 && ActiveStack->Size != 0) {
            LINEAR Base = ActiveStack->Base;
            LINEAR Top = Base + (LINEAR)ActiveStack->Size;

            if (CurrentSP < Base) {
                ERROR(TEXT("[GetCurrentStackFreeBytes] SP %p below stack base %p"), CurrentSP, Base);
            } else if (CurrentSP > Top) {
                ERROR(TEXT("[GetCurrentStackFreeBytes] SP %p above stack top %p"), CurrentSP, Top);
            } else {
                RemainingBytes = (UINT)(CurrentSP - Base);
            }
        } else {
            ERROR(TEXT("[GetCurrentStackFreeBytes] Unable to locate active stack for SP %p"), CurrentSP);
        }
    }

    if (!TaskValidated) {
        ERROR(TEXT("[GetCurrentStackFreeBytes] SAFE_USE_VALID_ID failed for current task %p"), CurrentTask);
        return 0;
    }

    return RemainingBytes;
}

/************************************************************************/

/**
 * @brief Expand the active stack by allocating additional space and migrating.
 * @param AdditionalBytes Requested growth amount (rounded and clamped).
 * @return TRUE on successful expansion, FALSE otherwise.
 */
BOOL GrowCurrentStack(UINT AdditionalBytes) {
    if (AdditionalBytes == 0) {
        AdditionalBytes = STACK_GROW_MIN_INCREMENT;
    }

    LPTASK CurrentTask = GetCurrentTask();
    if (CurrentTask == NULL) {
        ERROR(TEXT("[GrowCurrentStack] No current task"));
        return FALSE;
    }

    BOOL Success = FALSE;
    BOOL TaskValidated = FALSE;

    SAFE_USE_VALID_ID(CurrentTask, KOID_TASK) {
        TaskValidated = TRUE;

        do {
            LINEAR CurrentSP;
            GetESP(CurrentSP);

            LPSTACK ActiveStack = StackLocateActiveDescriptor(CurrentTask, CurrentSP);

            if (ActiveStack == NULL || ActiveStack->Base == 0 || ActiveStack->Size == 0) {
                ERROR(TEXT("[GrowCurrentStack] Active stack not found for SP %p"), CurrentSP);
                break;
            }

            LINEAR Base = ActiveStack->Base;
            UINT OldSize = ActiveStack->Size;
            LINEAR OldTop = Base + (LINEAR)OldSize;

            if (CurrentSP < Base || CurrentSP > OldTop) {
                ERROR(TEXT("[GrowCurrentStack] SP %p outside stack range [%p-%p]"), CurrentSP, Base, OldTop);
                break;
            }

            UINT UsedBytes = (UINT)(OldTop - CurrentSP);
            UINT DesiredAdditional = AdditionalBytes;

            if (DesiredAdditional < STACK_GROW_MIN_INCREMENT) {
                DesiredAdditional = STACK_GROW_MIN_INCREMENT;
            }

            UINT DesiredSize = OldSize + DesiredAdditional;
            DesiredSize = (UINT)PAGE_ALIGN(DesiredSize);

            if (DesiredSize <= OldSize) {
                DesiredSize = OldSize + PAGE_SIZE;
                DesiredSize = (UINT)PAGE_ALIGN(DesiredSize);
            }

            U32 Flags = ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER;

            DEBUG(TEXT("[GrowCurrentStack] Base=%p Size=%u SP=%p Used=%u NewSize=%u"),
                Base,
                OldSize,
                CurrentSP,
                UsedBytes,
                DesiredSize);

            if (ResizeRegion(Base, 0, OldSize, DesiredSize, Flags) == FALSE) {
                ERROR(TEXT("[GrowCurrentStack] ResizeRegion failed for base=%p size=%u -> %u"), Base, OldSize, DesiredSize);
                break;
            }

            LINEAR NewTop = Base + (LINEAR)DesiredSize;
            UINT CopySize = UsedBytes;

            if (CopySize < STACK_SAFETY_MARGIN) {
                CopySize = STACK_SAFETY_MARGIN;
                if (CopySize > OldSize) {
                    CopySize = OldSize;
                }
            }

            if (CopySize == 0) {
                CopySize = OldSize;
            }

            if (SwitchStack(NewTop, OldTop, CopySize) == FALSE) {
                ERROR(TEXT("[GrowCurrentStack] SwitchStack failed (DestTop=%p SourceTop=%p Size=%u)"), NewTop, OldTop, CopySize);

                if (ResizeRegion(Base, 0, DesiredSize, OldSize, Flags) == FALSE) {
                    ERROR(TEXT("[GrowCurrentStack] Failed to roll back stack resize for base=%p"), Base);
                }

                break;
            }

            ActiveStack->Size = DesiredSize;

            LINEAR UpdatedSP;
            GetESP(UpdatedSP);
            UINT RemainingBytes = (UINT)(UpdatedSP - Base);

#if defined(__EXOS_ARCH_I386__)
            if (ActiveStack == &(CurrentTask->Arch.Stack)) {
                LINEAR Delta = NewTop - OldTop;
                CurrentTask->Arch.Context.Registers.ESP += (UINT)Delta;
                CurrentTask->Arch.Context.Registers.EBP += (UINT)Delta;
            } else if (ActiveStack == &(CurrentTask->Arch.SysStack)) {
                LINEAR SysTop = ActiveStack->Base + (LINEAR)ActiveStack->Size;
                CurrentTask->Arch.Context.ESP0 = (U32)(SysTop - STACK_SAFETY_MARGIN);
            }
#else
            if (ActiveStack == &(CurrentTask->Arch.Stack)) {
                LINEAR Delta = NewTop - OldTop;
                CurrentTask->Arch.Context.Registers.RSP += (U64)Delta;
                CurrentTask->Arch.Context.Registers.RBP += (U64)Delta;
            } else if (ActiveStack == &(CurrentTask->Arch.SysStack)) {
                LINEAR SysTop = ActiveStack->Base + (LINEAR)ActiveStack->Size;
                CurrentTask->Arch.Context.RSP0 = (U64)(SysTop - STACK_SAFETY_MARGIN);
            }
#endif

            DEBUG(TEXT("[GrowCurrentStack] Resize complete: Size=%u Remaining=%u SP=%p"),
                ActiveStack->Size,
                RemainingBytes,
                UpdatedSP);

            Success = TRUE;
        } while (0);
    }

    if (!TaskValidated) {
        ERROR(TEXT("[GrowCurrentStack] SAFE_USE_VALID_ID failed for current task %p"), CurrentTask);
    }

    return Success;
}

/************************************************************************/

/**
 * @brief Guarantee at least the requested stack headroom, growing if needed.
 * @param MinimumFreeBytes Free byte threshold to enforce.
 * @return TRUE when the stack already meets or successfully reaches the quota.
 */
BOOL EnsureCurrentStackSpace(UINT MinimumFreeBytes) {
    if (MinimumFreeBytes == 0) {
        return TRUE;
    }

    UINT Remaining = GetCurrentStackFreeBytes();

    if (Remaining == MAX_UINT) {
        return TRUE;
    }

    if (Remaining >= MinimumFreeBytes) {
        return TRUE;
    }

    UINT Required = MinimumFreeBytes - Remaining;
    UINT Additional = Required + STACK_GROW_EXTRA_HEADROOM;

    DEBUG(TEXT("[EnsureCurrentStackSpace] Remaining=%u Required=%u Additional=%u"),
        Remaining,
        MinimumFreeBytes,
        Additional);

    return GrowCurrentStack(Additional);
}
