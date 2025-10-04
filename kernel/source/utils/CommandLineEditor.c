
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


    Command Line Editor

\************************************************************************/

#include "utils/CommandLineEditor.h"

#include "Console.h"
#include "Heap.h"
#include "drivers/Keyboard.h"
#include "Log.h"
#include "String.h"
#include "Task.h"
#include "VKey.h"

/***************************************************************************/

static void UpdateInputCursor(U32 StartX, U32 StartY, U32 CursorPos) {
    U32 TargetX = StartX + CursorPos;
    U32 TargetY = StartY;

    if (Console.Width != 0) {
        TargetY += TargetX / Console.Width;
        TargetX %= Console.Width;
    }

    SetConsoleCursorPosition(TargetX, TargetY);
}

/***************************************************************************/

static void RefreshInputDisplay(
    LPCSTR Buffer,
    U32 StartX,
    U32 StartY,
    U32 Length,
    U32 PreviousLength,
    U32 CursorPos,
    BOOL MaskCharacters) {
    U32 Index;

    SetConsoleCursorPosition(StartX, StartY);

    for (Index = 0; Index < Length; Index++) {
        if (MaskCharacters) {
            ConsolePrintChar('*');
        } else {
            ConsolePrintChar(Buffer[Index]);
        }
    }

    for (Index = Length; Index < PreviousLength; Index++) {
        ConsolePrintChar(STR_SPACE);
    }

    UpdateInputCursor(StartX, StartY, CursorPos);
}

/***************************************************************************/

void CommandLineEditorInit(LPCOMMANDLINEEDITOR Editor, U32 HistoryCapacity) {
    DEBUG(TEXT("[CommandLineEditorInit] Enter"));

    MemorySet(Editor, 0, sizeof(COMMANDLINEEDITOR));

    Editor->HistoryCapacity = HistoryCapacity;
    StringArrayInit(&Editor->History, HistoryCapacity);
    Editor->CompletionCallback = NULL;
    Editor->CompletionUserData = NULL;

    DEBUG(TEXT("[CommandLineEditorInit] Exit"));
}

/***************************************************************************/

void CommandLineEditorDeinit(LPCOMMANDLINEEDITOR Editor) {
    DEBUG(TEXT("[CommandLineEditorDeinit] Enter"));

    StringArrayDeinit(&Editor->History);
    Editor->HistoryCapacity = 0;
    Editor->CompletionCallback = NULL;
    Editor->CompletionUserData = NULL;

    DEBUG(TEXT("[CommandLineEditorDeinit] Exit"));
}

/***************************************************************************/

void CommandLineEditorSetCompletionCallback(
    LPCOMMANDLINEEDITOR Editor,
    COMMANDLINEEDITOR_COMPLETION_CALLBACK Callback,
    LPVOID UserData) {
    Editor->CompletionCallback = Callback;
    Editor->CompletionUserData = UserData;
}

/***************************************************************************/

BOOL CommandLineEditorReadLine(
    LPCOMMANDLINEEDITOR Editor,
    LPSTR Buffer,
    U32 BufferSize,
    BOOL MaskCharacters) {
    KEYCODE KeyCode;
    U32 CursorPos = 0;
    U32 Length = 0;
    U32 DisplayedLength = 0;
    U32 HistoryPos = Editor->History.Count;
    U32 StartX = 0;
    U32 StartY = 0;

    DEBUG(TEXT("[CommandLineEditorReadLine] Enter"));

    if (BufferSize == 0) return FALSE;

    Buffer[0] = STR_NULL;
    GetConsoleCursorPosition(&StartX, &StartY);

    FOREVER {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            if (KeyCode.VirtualKey == VK_ESCAPE) {
                Length = 0;
                CursorPos = 0;
                Buffer[0] = STR_NULL;
                RefreshInputDisplay(
                    Buffer, StartX, StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                DisplayedLength = Length;
            } else if (KeyCode.VirtualKey == VK_BACKSPACE) {
                if (CursorPos > 0) {
                    MemoryMove(Buffer + CursorPos - 1, Buffer + CursorPos, (Length - CursorPos) + 1);
                    CursorPos--;
                    Length--;
                    RefreshInputDisplay(
                        Buffer, StartX, StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                    DisplayedLength = Length;
                }
            } else if (KeyCode.VirtualKey == VK_DELETE) {
                if (CursorPos < Length) {
                    MemoryMove(Buffer + CursorPos, Buffer + CursorPos + 1, (Length - CursorPos));
                    Length--;
                    Buffer[Length] = STR_NULL;
                    RefreshInputDisplay(
                        Buffer, StartX, StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                    DisplayedLength = Length;
                }
            } else if (KeyCode.VirtualKey == VK_LEFT) {
                if (CursorPos > 0) {
                    CursorPos--;
                    UpdateInputCursor(StartX, StartY, CursorPos);
                }
            } else if (KeyCode.VirtualKey == VK_RIGHT) {
                if (CursorPos < Length) {
                    CursorPos++;
                    UpdateInputCursor(StartX, StartY, CursorPos);
                }
            } else if (KeyCode.VirtualKey == VK_HOME) {
                CursorPos = 0;
                UpdateInputCursor(StartX, StartY, CursorPos);
            } else if (KeyCode.VirtualKey == VK_END) {
                CursorPos = Length;
                UpdateInputCursor(StartX, StartY, CursorPos);
            } else if (KeyCode.VirtualKey == VK_ENTER) {
                ConsolePrintChar(STR_NEWLINE);
                Buffer[Length] = STR_NULL;
                DEBUG(
                    TEXT("[CommandLineEditorReadLine] ENTER pressed, final buffer: '%s', length=%d"),
                    Buffer,
                    Length);
                break;
            } else if (KeyCode.VirtualKey == VK_UP) {
                if (HistoryPos > 0) {
                    HistoryPos--;
                    StringCopy(Buffer, StringArrayGet(&Editor->History, HistoryPos));
                    Length = StringLength(Buffer);
                    CursorPos = Length;
                    RefreshInputDisplay(
                        Buffer, StartX, StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                    DisplayedLength = Length;
                }
            } else if (KeyCode.VirtualKey == VK_DOWN) {
                if (HistoryPos < Editor->History.Count) HistoryPos++;
                if (HistoryPos == Editor->History.Count) {
                    Buffer[0] = STR_NULL;
                    Length = 0;
                    CursorPos = 0;
                } else {
                    StringCopy(Buffer, StringArrayGet(&Editor->History, HistoryPos));
                    Length = StringLength(Buffer);
                    CursorPos = Length;
                }
                RefreshInputDisplay(
                    Buffer, StartX, StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                DisplayedLength = Length;
            } else if (KeyCode.VirtualKey == VK_TAB) {
                if (Editor->CompletionCallback) {
                    STR Replacement[MAX_PATH_NAME];
                    U32 Start = CursorPos;

                    while (Start && Buffer[Start - 1] != STR_SPACE) {
                        Start--;
                    }

                    {
                        COMMANDLINE_COMPLETION_CONTEXT CompletionContext;
                        CompletionContext.Buffer = Buffer;
                        CompletionContext.BufferLength = Length;
                        CompletionContext.CursorPosition = CursorPos;
                        CompletionContext.TokenStart = Start;
                        CompletionContext.Token = Buffer + Start;
                        CompletionContext.TokenLength = CursorPos - Start;
                        CompletionContext.UserData = Editor->CompletionUserData;

                        if (Editor->CompletionCallback(
                                &CompletionContext,
                                Replacement,
                                MAX_PATH_NAME)) {
                            U32 TokenLength = CursorPos - Start;
                            U32 ReplacementLength = StringLength(Replacement);
                            U32 TailLength = Length - CursorPos;
                            U32 NewLength = Length - TokenLength + ReplacementLength;

                            if (NewLength < BufferSize) {
                                MemoryMove(
                                    Buffer + Start + ReplacementLength,
                                    Buffer + CursorPos,
                                    TailLength + 1);
                                MemoryCopy(Buffer + Start, Replacement, ReplacementLength);
                                Length = NewLength;
                                CursorPos = Start + ReplacementLength;
                                RefreshInputDisplay(
                                    Buffer,
                                    StartX,
                                    StartY,
                                    Length,
                                    DisplayedLength,
                                    CursorPos,
                                    MaskCharacters);
                                DisplayedLength = Length;
                            }
                        }
                    }
                }
            } else if (KeyCode.ASCIICode >= STR_SPACE) {
                if (Length < BufferSize - 1) {
                    MemoryMove(Buffer + CursorPos + 1, Buffer + CursorPos, (Length - CursorPos) + 1);
                    Buffer[CursorPos] = KeyCode.ASCIICode;
                    CursorPos++;
                    Length++;
                    Buffer[Length] = STR_NULL;
                    RefreshInputDisplay(
                        Buffer, StartX, StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                    DisplayedLength = Length;
                }
            }
        }

        Sleep(10);
    }

    DEBUG(TEXT("[CommandLineEditorReadLine] Exit"));

    return TRUE;
}

/***************************************************************************/

void CommandLineEditorRemember(
    LPCOMMANDLINEEDITOR Editor,
    LPCSTR CommandLine) {
    if (StringLength(CommandLine) == 0) return;
    StringArrayMoveToEnd(&Editor->History, CommandLine);
}

/***************************************************************************/

void CommandLineEditorClearHistory(LPCOMMANDLINEEDITOR Editor) {
    U32 Index;

    if (Editor->History.Items == NULL) return;

    for (Index = 0; Index < Editor->History.Count; Index++) {
        if (Editor->History.Items[Index]) {
            KernelHeapFree(Editor->History.Items[Index]);
            Editor->History.Items[Index] = NULL;
        }
    }

    Editor->History.Count = 0;
}

/***************************************************************************/
