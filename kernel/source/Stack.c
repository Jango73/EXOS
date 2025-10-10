
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
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "Process.h"
#include "String.h"

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
BOOL CopyStackWithEBP(LINEAR DestStackTop, LINEAR SourceStackTop, U32 Size, LINEAR StartEBP) {
    if (!DestStackTop || !SourceStackTop || Size == 0) {
        return FALSE;
    }

    LINEAR SourceStackStart = SourceStackTop - Size;
    LINEAR DestStackStart = DestStackTop - Size;
    I32 Delta = DestStackTop - SourceStackTop;

    // Copy stack content from source to destination
    MemoryCopy((void *)DestStackStart, (const void *)SourceStackStart, Size);

    // Walk the frame chain and adjust all EBP values
    LINEAR CurrentEbp = StartEBP;

    // Only adjust if current EBP is within source stack range
    if (CurrentEbp >= SourceStackStart && CurrentEbp < SourceStackTop) {
        LINEAR AdjustedCurrentEbp = CurrentEbp + Delta;
        LINEAR WalkEbp = AdjustedCurrentEbp;

        while (WalkEbp >= DestStackStart && WalkEbp < DestStackTop) {
            U32 *Fp = (U32 *)WalkEbp;
            U32 SavedEbp = Fp[0];

            // If saved EBP points into the source stack, adjust it
            if (SavedEbp >= SourceStackStart && SavedEbp < SourceStackTop) {
                U32 NewSavedEbp = SavedEbp + Delta;
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
BOOL CopyStack(LINEAR DestStackTop, LINEAR SourceStackTop, U32 Size) {
#if defined(__EXOS_ARCH_I386__)
    LINEAR CurrentEbp;
    GetEBP(CurrentEbp);
    return CopyStackWithEBP(DestStackTop, SourceStackTop, Size, CurrentEbp);
#else
    return CopyStackWithEBP(DestStackTop, SourceStackTop, Size, GetEBP());
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
BOOL SwitchStack(LINEAR DestStackTop, LINEAR SourceStackTop, U32 Size) {
#if defined(__EXOS_ARCH_I386__)
    if (!CopyStack(DestStackTop, SourceStackTop, Size)) {
        return FALSE;
    }

    LINEAR SourceStackStart = SourceStackTop - Size;
    I32 Delta = DestStackTop - SourceStackTop;

    // Get current ESP and EBP at the moment of switch
    LINEAR CurrentEsp;
    LINEAR CurrentEbp;
#if defined(__EXOS_ARCH_I386__)
    GetESP(CurrentEsp);
    GetEBP(CurrentEbp);
#else
    CurrentEsp = GetESP();
    CurrentEbp = GetEBP();
#endif

    DEBUG(TEXT("[SwitchStack] Current ESP=%X, EBP=%X at switch time"), CurrentEsp, CurrentEbp);

    // Check if we're within the source stack range
    if (CurrentEsp >= SourceStackStart && CurrentEsp < SourceStackTop) {
        LINEAR NewEsp = CurrentEsp + Delta;
        LINEAR NewEbp = CurrentEbp + Delta;

        DEBUG(TEXT("[SwitchStack] Switching ESP %X -> %X, EBP %X -> %X"), CurrentEsp, NewEsp, CurrentEbp,
            NewEbp);

        // Switch ESP and EBP
        __asm__ __volatile__(
            "movl %0, %%esp\n\t"
            "movl %1, %%ebp"
            :
            : "r"(NewEsp), "r"(NewEbp)
            : "memory");

        return TRUE;
    } else {
        DEBUG(TEXT("[SwitchStack] ESP %X not in source stack range [%X-%X]"), CurrentEsp, SourceStackStart,
            SourceStackTop);
        return FALSE;
    }
#else
    UNUSED(DestStackTop);
    UNUSED(SourceStackTop);
    UNUSED(Size);

    WARNING(TEXT("[SwitchStack] Not implemented for this architecture"));

    return FALSE;
#endif
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
        StackBase = CurrentTask->Arch.StackBase;
        StackTop = StackBase + CurrentTask->Arch.StackSize;
    } else if (InKernelMode) {
        // User task currently in kernel mode (via syscall/interrupt)
        // The hardware switches to ESP0 stack, which may not be the task's system stack
        // We cannot reliably validate the current ESP since it might be on a different kernel stack
        // Instead, we just verify the task has a valid system stack allocated
        if (CurrentTask->Arch.SysStackBase == 0 || CurrentTask->Arch.SysStackSize == 0) {
            ERROR(TEXT("[CheckStack] User task in kernel mode without system stack!"));
            KernelLogText(
                LOG_ERROR, TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name,
                CurrentTask->Process->FileName);
            return FALSE;
        }
        // For userland tasks in kernel mode, skip ESP validation as the current ESP
        // might be on the TSS ESP0 stack or another kernel stack, not the task's system stack
        return TRUE;
    } else {
        // User task in user mode - check saved user stack ESP
        CurrentESP = StackGetSavedPointer(CurrentTask);
        StackBase = CurrentTask->Arch.StackBase;
        StackTop = StackBase + CurrentTask->Arch.StackSize;
    }

    if (CurrentESP < StackBase || CurrentESP > StackTop) {
        ERROR(TEXT("[CheckStack] ESP OUTSIDE STACK BOUNDS!"));
        KernelLogText(
            LOG_ERROR, TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name,
            CurrentTask->Process->FileName);
        ERROR(TEXT("[CheckStack] ESP: %x"), CurrentESP);
        ERROR(TEXT("[CheckStack] StackBase: %x"), StackBase);
        ERROR(TEXT("[CheckStack] StackTop: %x"), StackTop);
        ERROR(TEXT("[CheckStack] InKernelMode: %u"), InKernelMode ? 1 : 0);

        if (CurrentESP < StackBase) {
            KernelLogText(
                LOG_ERROR, TEXT("[CheckStack] ESP is %u bytes below stack base (severe underflow)"),
                StackBase - CurrentESP);
        } else {
            KernelLogText(
                LOG_ERROR, TEXT("[CheckStack] ESP is %u bytes above stack top (overflow)"), CurrentESP - StackTop);
        }

        return FALSE;
    }

    if (CurrentESP <= (StackBase + STACK_SAFETY_MARGIN)) {
        ERROR(TEXT("[CheckStack] STACK OVERFLOW DETECTED!"));
        KernelLogText(
            LOG_ERROR, TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name,
            CurrentTask->Process->FileName);
        ERROR(TEXT("[CheckStack] Func: %x"), CurrentTask ? CurrentTask->Function : 0);
        ERROR(TEXT("[CheckStack] ESP: %x"), CurrentESP);
        ERROR(TEXT("[CheckStack] StackBase: %x"), StackBase);
        ERROR(TEXT("[CheckStack] StackTop: %x"), StackTop);
        ERROR(TEXT("[CheckStack] InKernelMode: %u"), InKernelMode ? 1 : 0);
        KernelLogText(
            LOG_ERROR, TEXT("[CheckStack] Safety margin violated by %u bytes"),
            (StackBase + STACK_SAFETY_MARGIN) - CurrentESP);
        return FALSE;
    }

    return TRUE;
}
