
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

/***************************************************************************/

#define STACK_SAFETY_MARGIN 256
#define TEST_STACK_SIZE 256

/***************************************************************************/

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

BOOL CopyStack(LINEAR DestStackTop, LINEAR SourceStackTop, U32 Size) {
    return CopyStackWithEBP(DestStackTop, SourceStackTop, Size, GetEBP());
}

/***************************************************************************/

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

/***************************************************************************/

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

    __asm__ volatile("movl %%esp, %0" : "=r" (CurrentESP));
    __asm__ volatile("movw %%cs, %%ax; movl %%eax, %0" : "=r" (CurrentCS) : : "eax");

    InKernelMode = ((CurrentCS & SELECTOR_RPL_MASK) == 0);

    if (InKernelMode) {
        StackBase = CurrentTask->SysStackBase;                    
        StackTop = StackBase + CurrentTask->SysStackSize;         
    } else {
        StackBase = CurrentTask->StackBase;                       
        StackTop = StackBase + CurrentTask->StackSize;            
    }

    if (CurrentESP <= (StackBase + STACK_SAFETY_MARGIN)) {
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] STACK OVERFLOW DETECTED!"));
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] Task: %X"), CurrentTask);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] ESP: %X"), CurrentESP);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] StackBase: %X"), StackBase);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] StackTop: %X"), StackTop);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] InKernelMode: %u"), InKernelMode ? 1 : 0);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] Safety margin violated by %u bytes"), 
                     (StackBase + STACK_SAFETY_MARGIN) - CurrentESP);
        return FALSE;
    }

    if (CurrentESP < StackBase || CurrentESP >= StackTop) {
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] ESP OUTSIDE STACK BOUNDS!"));
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] Task: %X"), CurrentTask);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] ESP: %X"), CurrentESP);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] StackBase: %X"), StackBase);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] StackTop: %X"), StackTop);
        KernelLogText(LOG_ERROR, TEXT("[CheckStack] InKernelMode: %u"), InKernelMode ? 1 : 0);

        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

BOOL TestCopyStack(void) {
    U8 SourceStack[TEST_STACK_SIZE];
    U8 DestStack[TEST_STACK_SIZE];
    U32 *SourcePtr, *DestPtr;
    LINEAR SourceStackTop, DestStackTop;
    I32 Delta;
    BOOL TestPassed = TRUE;
    
    KernelLogText(LOG_DEBUG, TEXT("[TestCopyStack] Starting CopyStack test"));
    
    SourceStackTop = (LINEAR)(SourceStack + TEST_STACK_SIZE);
    DestStackTop = (LINEAR)(DestStack + TEST_STACK_SIZE);
    Delta = DestStackTop - SourceStackTop;
    
    KernelLogText(LOG_DEBUG, TEXT("[TestCopyStack] SourceStackTop=%X, DestStackTop=%X, Delta=%X"), 
                  SourceStackTop, DestStackTop, Delta);
    
    MemorySet(SourceStack, 0xAA, TEST_STACK_SIZE);
    MemorySet(DestStack, 0x55, TEST_STACK_SIZE);
    
    SourcePtr = (U32*)(SourceStackTop - 16);  // Frame 1 EBP
    *SourcePtr = (U32)(SourceStackTop - 32); // Points to Frame 2
    SourcePtr = (U32*)(SourceStackTop - 12);  // Frame 1 return addr
    *SourcePtr = 0x12345678;
    
    SourcePtr = (U32*)(SourceStackTop - 32);  // Frame 2 EBP  
    *SourcePtr = (U32)(SourceStackTop - 48);  // Points to Frame 3
    SourcePtr = (U32*)(SourceStackTop - 28);  // Frame 2 return addr
    *SourcePtr = 0x9ABCDEF0;
    
    SourcePtr = (U32*)(SourceStackTop - 48);  // Frame 3 EBP
    *SourcePtr = 0x1000;  // Points outside stack (should not be adjusted)
    SourcePtr = (U32*)(SourceStackTop - 44);  // Frame 3 return addr
    *SourcePtr = 0xDEADBEEF;
    
    KernelLogText(LOG_DEBUG, TEXT("[TestCopyStack] Source stack populated with test frames"));
    
    if (!CopyStackWithEBP(DestStackTop, SourceStackTop, TEST_STACK_SIZE, (LINEAR)(SourceStackTop - 16))) {
        KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] CopyStack failed"));
        return FALSE;
    }
    
    DestPtr = (U32*)(DestStackTop - 16);  // Frame 1 EBP in dest
    U32 ExpectedEbp1 = (SourceStackTop - 32) + Delta;
    if (*DestPtr != ExpectedEbp1) {
        KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] Frame 1 EBP: expected %X, got %X"), 
                      ExpectedEbp1, *DestPtr);
        TestPassed = FALSE;
    }
    
    DestPtr = (U32*)(DestStackTop - 12);  // Frame 1 return addr
    if (*DestPtr != 0x12345678) {
        KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] Frame 1 return addr: expected 0x12345678, got %X"), 
                      *DestPtr);
        TestPassed = FALSE;
    }
    
    DestPtr = (U32*)(DestStackTop - 32);  // Frame 2 EBP in dest
    U32 ExpectedEbp2 = (SourceStackTop - 48) + Delta;
    if (*DestPtr != ExpectedEbp2) {
        KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] Frame 2 EBP: expected %X, got %X"), 
                      ExpectedEbp2, *DestPtr);
        TestPassed = FALSE;
    }
    
    DestPtr = (U32*)(DestStackTop - 28);  // Frame 2 return addr
    if (*DestPtr != 0x9ABCDEF0) {
        KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] Frame 2 return addr: expected 0x9ABCDEF0, got %X"), 
                      *DestPtr);
        TestPassed = FALSE;
    }
    
    DestPtr = (U32*)(DestStackTop - 48);  // Frame 3 EBP in dest (should NOT be adjusted)
    if (*DestPtr != 0x1000) {
        KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] Frame 3 EBP: expected 0x1000 (unchanged), got %X"), 
                      *DestPtr);
        TestPassed = FALSE;
    }
    
    DestPtr = (U32*)(DestStackTop - 44);  // Frame 3 return addr
    if (*DestPtr != 0xDEADBEEF) {
        KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] Frame 3 return addr: expected 0xDEADBEEF, got %X"), 
                      *DestPtr);
        TestPassed = FALSE;
    }
    
    for (U32 i = 0; i < TEST_STACK_SIZE - 48; i++) {
        if (DestStack[i] != 0xAA) {
            KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] Non-frame data corrupted at offset %u: expected 0xAA, got %X"), 
                          i, DestStack[i]);
            TestPassed = FALSE;
            break;
        }
    }
    
    if (TestPassed) {
        KernelLogText(LOG_DEBUG, TEXT("[TestCopyStack] TEST PASSED"));
    } else {
        KernelLogText(LOG_ERROR, TEXT("[TestCopyStack] TEST FAILED"));
    }
    
    return TestPassed;
}

/***************************************************************************/
