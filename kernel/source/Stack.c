
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

#include "../include/Base.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/Process.h"
#include "../include/System.h"

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

    KernelLogText(LOG_DEBUG, TEXT("[CopyStack] SourceStackTop=%X, DestStackTop=%X, Size=%X"), 
                  SourceStackTop, DestStackTop, Size);
    KernelLogText(LOG_DEBUG, TEXT("[CopyStack] Delta=%X"), Delta);

    // Copy stack content from source to destination
    MemoryCopy((void*)DestStackStart, (const void*)SourceStackStart, Size);

    // Walk the frame chain and adjust all EBP values
    LINEAR CurrentEbp = StartEBP;
    
    // Only adjust if current EBP is within source stack range
    if (CurrentEbp >= SourceStackStart && CurrentEbp < SourceStackTop) {
        LINEAR AdjustedCurrentEbp = CurrentEbp + Delta;
        LINEAR WalkEbp = AdjustedCurrentEbp;
        
        KernelLogText(LOG_DEBUG, TEXT("[CopyStack] Adjusting frames, CurrentEBP=%X -> %X"), 
                      CurrentEbp, AdjustedCurrentEbp);

        while (WalkEbp >= DestStackStart && WalkEbp < DestStackTop) {
            U32 *Fp = (U32*)WalkEbp;
            U32 SavedEbp = Fp[0];
            
            KernelLogText(LOG_DEBUG, TEXT("[CopyStack] Frame at %X, SavedEBP=%X"), WalkEbp, SavedEbp);
            
            // If saved EBP points into the source stack, adjust it
            if (SavedEbp >= SourceStackStart && SavedEbp < SourceStackTop) {
                U32 NewSavedEbp = SavedEbp + Delta;
                Fp[0] = NewSavedEbp;
                WalkEbp = NewSavedEbp;  // Continue with adjusted value
                KernelLogText(LOG_DEBUG, TEXT("[CopyStack] Adjusted SavedEBP %X -> %X"), SavedEbp, NewSavedEbp);
            } else {
                KernelLogText(LOG_DEBUG, TEXT("[CopyStack] SavedEBP %X outside stack range, stopping"), SavedEbp);
                break;  // End of chain or points outside our stack
            }
        }
        
        return TRUE;
    } else {
        KernelLogText(LOG_DEBUG, TEXT("[CopyStack] Current EBP %X not in source stack range"), CurrentEbp);
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
    return CopyStackWithEBP(DestStackTop, SourceStackTop, Size, GetEBP());
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
    if (!CopyStack(DestStackTop, SourceStackTop, Size)) {
        return FALSE;
    }
    
    LINEAR SourceStackStart = SourceStackTop - Size;
    I32 Delta = DestStackTop - SourceStackTop;
    
    // Get current ESP and EBP at the moment of switch
    LINEAR CurrentEsp = GetESP();
    LINEAR CurrentEbp = GetEBP();
    
    KernelLogText(LOG_DEBUG, TEXT("[SwitchStack] Current ESP=%X, EBP=%X at switch time"), 
                  CurrentEsp, CurrentEbp);
    
    // Check if we're within the source stack range
    if (CurrentEsp >= SourceStackStart && CurrentEsp < SourceStackTop) {
        LINEAR NewEsp = CurrentEsp + Delta;
        LINEAR NewEbp = CurrentEbp + Delta;
        
        KernelLogText(LOG_DEBUG, TEXT("[SwitchStack] Switching ESP %X -> %X, EBP %X -> %X"), 
                      CurrentEsp, NewEsp, CurrentEbp, NewEbp);
        
        // Switch ESP and EBP
        __asm__ __volatile__(
            "movl %0, %%esp\n\t"
            "movl %1, %%ebp"
            : 
            : "r" (NewEsp), "r" (NewEbp)
            : "memory"
        );
        
        return TRUE;
    } else {
        KernelLogText(LOG_DEBUG, TEXT("[SwitchStack] ESP %X not in source stack range [%X-%X]"), 
                      CurrentEsp, SourceStackStart, SourceStackTop);
        return FALSE;
    }
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
    U32 CurrentESP;
    U32 CurrentCS;
    U32 StackBase, StackTop;
    BOOL InKernelMode;

    CurrentTask = GetCurrentTask();

    if (CurrentTask == NULL) {
        return TRUE;
    }

    // Skip stack checking for main kernel task since ESP is not saved in context
    if (CurrentTask->Flags & TASK_CREATE_MAIN_KERNEL) {
        return TRUE;
    }

    GetCS(CurrentCS);
    InKernelMode = ((CurrentCS & SELECTOR_RPL_MASK) == 0);

    // Determine which ESP to check and which stack bounds to use
    if (CurrentTask->Process->Privilege == PRIVILEGE_KERNEL) {
        // Kernel tasks always use their normal stack
        CurrentESP = CurrentTask->Context.Registers.ESP;
        StackBase = CurrentTask->StackBase;                       
        StackTop = StackBase + CurrentTask->StackSize;            
    } else if (InKernelMode) {
        // User task currently in kernel mode (via syscall/interrupt)
        // The hardware switches to ESP0 stack, which may not be the task's system stack
        // We cannot reliably validate the current ESP since it might be on a different kernel stack
        // Instead, we just verify the task has a valid system stack allocated
        if (CurrentTask->SysStackBase == 0 || CurrentTask->SysStackSize == 0) {
            KernelLogText(LOG_ERROR, TEXT("[CheckStack] User task in kernel mode without system stack!"));
            KernelLogText(LOG_ERROR, TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name, CurrentTask->Process->FileName);
            return FALSE;
        }
        // For userland tasks in kernel mode, skip ESP validation as the current ESP
        // might be on the TSS ESP0 stack or another kernel stack, not the task's system stack
        return TRUE;
    } else {
        // User task in user mode - check saved user stack ESP
        CurrentESP = CurrentTask->Context.Registers.ESP;
        StackBase = CurrentTask->StackBase;                       
        StackTop = StackBase + CurrentTask->StackSize;            
    }

    if (CurrentESP < StackBase || CurrentESP > StackTop) {
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] ESP OUTSIDE STACK BOUNDS!"));
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name, CurrentTask->Process->FileName);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] ESP: %x"), CurrentESP);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] StackBase: %x"), StackBase);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] StackTop: %x"), StackTop);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] InKernelMode: %u"), InKernelMode ? 1 : 0);
        
        if (CurrentESP < StackBase) {
            KernelLogText(LOG_ERROR, TEXT("[CheckStack] ESP is %u bytes below stack base (severe underflow)"), 
                         StackBase - CurrentESP);
        } else {
            KernelLogText(LOG_ERROR, TEXT("[CheckStack] ESP is %u bytes above stack top (overflow)"), 
                         CurrentESP - StackTop);
        }

        return FALSE;
    }
    
    if (CurrentESP <= (StackBase + STACK_SAFETY_MARGIN)) {
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] STACK OVERFLOW DETECTED!"));
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] Task: %x (%s @ %s)"), CurrentTask, CurrentTask->Name, CurrentTask->Process->FileName);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] Func: %x"), CurrentTask ? CurrentTask->Function : 0);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] ESP: %x"), CurrentESP);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] StackBase: %x"), StackBase);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] StackTop: %x"), StackTop);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] InKernelMode: %u"), InKernelMode ? 1 : 0);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] Safety margin violated by %u bytes"), 
                     (StackBase + STACK_SAFETY_MARGIN) - CurrentESP);
        return FALSE;
    }

    return TRUE;
}
