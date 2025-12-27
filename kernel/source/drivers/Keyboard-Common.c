
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


    Keyboard Common

\************************************************************************/

#include "drivers/Keyboard.h"

#include "Console.h"
#include "Log.h"
#include "Memory.h"
#include "process/Process.h"
#include "process/Task.h"
#include "User.h"

/***************************************************************************/

KEYBOARDSTRUCT Keyboard = {
    .Mutex = EMPTY_MUTEX,
    .Shift = 1,
    .Control = 0,
    .Alt = 0,
    .CapsLock = 0,
    .NumLock = 0,
    .ScrollLock = 0,
    .Pause = 0,
    .Buffer = {{0}},
    .Status = {0}};

/***************************************************************************/

static void SendKeyCodeToBuffer(LPKEYCODE KeyCode) {
    U32 Index;

    FINE_DEBUG(TEXT("[SendKeyCodeToBuffer] Enter"));

    if (KeyCode->VirtualKey != 0 || KeyCode->ASCIICode != 0) {
        //-------------------------------------
        // Put the key in the buffer

        for (Index = 0; Index < MAXKEYBUFFER; Index++) {
            if (Keyboard.Buffer[Index].VirtualKey == 0 && Keyboard.Buffer[Index].ASCIICode == 0) {
                Keyboard.Buffer[Index] = *KeyCode;
                break;
            }
        }
    }

    FINE_DEBUG(TEXT("[SendKeyCodeToBuffer] Exit"));
}

/***************************************************************************/

static BOOL DispatchKeyMessage(LPKEYCODE KeyCode) {
    if (KeyCode == NULL) return FALSE;
    if (KeyCode->VirtualKey == 0 && KeyCode->ASCIICode == 0) return FALSE;

    return EnqueueInputMessage(EWM_KEYDOWN, KeyCode->VirtualKey, KeyCode->ASCIICode);
}

/***************************************************************************/

void RouteKeyCode(LPKEYCODE KeyCode) {
    if (DispatchKeyMessage(KeyCode) == FALSE) {
        SendKeyCodeToBuffer(KeyCode);
    }
}

/***************************************************************************/

static BOOL FetchKeyFromMessageQueue(BOOL RemoveKeyDown, BOOL PurgeKeyUp, LPKEYCODE KeyCode) {
    LPTASK Task = GetCurrentTask();
    LPPROCESS Process = NULL;
    LPLIST MessageList = NULL;
    BOOL Found = FALSE;

    if (KeyCode == NULL) return FALSE;

    SAFE_USE_VALID_ID(Task, KOID_TASK) { Process = Task->Process; }
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) { MessageList = Process->MessageQueue.Messages; }

    if (MessageList == NULL) return FALSE;

    LockMutex(&(Process->Mutex), INFINITY);
    LockMutex(&(Process->MessageQueue.Mutex), INFINITY);

    LPLISTNODE Node = MessageList->First;
    while (Node != NULL) {
        LPLISTNODE Next = Node->Next;
        LPMESSAGE Message = (LPMESSAGE)Node;

        if (Message->Message == EWM_KEYUP) {
            if (PurgeKeyUp) {
                ListEraseItem(MessageList, Message);
            }
        } else if (Message->Message == EWM_KEYDOWN && Found == FALSE) {
            KeyCode->VirtualKey = Message->Param1;
            KeyCode->ASCIICode = (STR)Message->Param2;
            Found = TRUE;
            if (RemoveKeyDown) {
                ListEraseItem(MessageList, Message);
            }
        }

        Node = Next;
    }

    UnlockMutex(&(Process->MessageQueue.Mutex));
    UnlockMutex(&(Process->Mutex));

    return Found;
}

/***************************************************************************/

static BOOL PeekKeyInMessageQueue(LPKEYCODE KeyCode) {
    LPTASK Task = GetCurrentTask();
    LPPROCESS Process = NULL;
    LPLIST MessageList = NULL;
    BOOL Found = FALSE;

    if (KeyCode == NULL) return FALSE;

    SAFE_USE_VALID_ID(Task, KOID_TASK) { Process = Task->Process; }
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) { MessageList = Process->MessageQueue.Messages; }

    if (MessageList == NULL) return FALSE;

    LockMutex(&(Process->Mutex), INFINITY);
    LockMutex(&(Process->MessageQueue.Mutex), INFINITY);

    for (LPLISTNODE Node = MessageList->First; Node != NULL; Node = Node->Next) {
        LPMESSAGE Message = (LPMESSAGE)Node;
        if (Message->Message == EWM_KEYDOWN) {
            KeyCode->VirtualKey = Message->Param1;
            KeyCode->ASCIICode = (STR)Message->Param2;
            Found = TRUE;
            break;
        }
    }

    UnlockMutex(&(Process->MessageQueue.Mutex));
    UnlockMutex(&(Process->Mutex));

    return Found;
}

/***************************************************************************/

BOOL PeekChar(void) {
    U32 Result = FALSE;
    KEYCODE KeyCode = {0};

    FINE_DEBUG(TEXT("[PeekChar] Enter"));

    if (PeekKeyInMessageQueue(&KeyCode) == TRUE) {
        return TRUE;
    }

    LockMutex(&(Keyboard.Mutex), INFINITY);

    if (Keyboard.Buffer[0].VirtualKey) Result = TRUE;
    if (Keyboard.Buffer[0].ASCIICode) Result = TRUE;

    UnlockMutex(&(Keyboard.Mutex));

    FINE_DEBUG(TEXT("[PeekChar] Exit"));

    return Result;
}

/***************************************************************************/

STR GetChar(void) {
    U32 Index;
    STR Char;
    KEYCODE KeyCode = {0};

    if (FetchKeyFromMessageQueue(TRUE, TRUE, &KeyCode) == TRUE) {
        return KeyCode.ASCIICode;
    }

    LockMutex(&(Keyboard.Mutex), INFINITY);

    Char = Keyboard.Buffer[0].ASCIICode;

    //-------------------------------------
    // Roll the keyboard buffer

    for (Index = 1; Index < MAXKEYBUFFER; Index++) {
        Keyboard.Buffer[Index - 1] = Keyboard.Buffer[Index];
    }

    Keyboard.Buffer[MAXKEYBUFFER - 1].VirtualKey = 0;
    Keyboard.Buffer[MAXKEYBUFFER - 1].ASCIICode = 0;

    UnlockMutex(&(Keyboard.Mutex));

    return Char;
}

/***************************************************************************/

BOOL GetKeyCode(LPKEYCODE KeyCode) {
    U32 Index;

    if (FetchKeyFromMessageQueue(TRUE, TRUE, KeyCode) == TRUE) {
        return TRUE;
    }

    LockMutex(&(Keyboard.Mutex), INFINITY);

    KeyCode->VirtualKey = Keyboard.Buffer[0].VirtualKey;
    KeyCode->ASCIICode = Keyboard.Buffer[0].ASCIICode;

    //-------------------------------------
    // Roll the keyboard buffer

    for (Index = 1; Index < MAXKEYBUFFER; Index++) {
        Keyboard.Buffer[Index - 1] = Keyboard.Buffer[Index];
    }

    Keyboard.Buffer[MAXKEYBUFFER - 1].VirtualKey = 0;
    Keyboard.Buffer[MAXKEYBUFFER - 1].ASCIICode = 0;

    UnlockMutex(&(Keyboard.Mutex));

    return TRUE;
}

/***************************************************************************/

void WaitKey(void) {
    ConsolePrint(TEXT("Press a key\n"));
    while (!PeekChar()) {
    }
    GetChar();
}

/***************************************************************************/

/**
 * @brief Clear buffered keyboard characters.
 */
void ClearKeyboardBuffer(void) {
    U32 Index;

    LockMutex(&(Keyboard.Mutex), INFINITY);

    for (Index = 0; Index < MAXKEYBUFFER; Index++) {
        Keyboard.Buffer[Index].VirtualKey = 0;
        Keyboard.Buffer[Index].ASCIICode = 0;
    }

    UnlockMutex(&(Keyboard.Mutex));
}

/***************************************************************************/
