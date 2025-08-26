
// Edit.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#include "../include/Base.h"
#include "../include/Console.h"
#include "../include/Heap.h"
#include "../include/Kernel.h"
#include "../include/Keyboard.h"
#include "../include/List.h"
#include "../include/Log.h"
#include "../include/User.h"
#include "../include/VKey.h"

/***************************************************************************/

#define MAX_COLUMNS 70
#define MAX_LINES 20

/***************************************************************************/

typedef struct tag_EDITLINE {
    LISTNODE_FIELDS
    I32 MaxChars;
    I32 NumChars;
    LPSTR Chars;
} EDITLINE, *LPEDITLINE;

/***************************************************************************/

typedef struct tag_EDITFILE {
    LISTNODE_FIELDS
    LPLIST Lines;
    POINT Cursor;
    POINT SelStart;
    POINT SelEnd;
    I32 Left;
    I32 Top;
} EDITFILE, *LPEDITFILE;

/***************************************************************************/

typedef struct tag_EDITCONTEXT {
    LISTNODE_FIELDS
    LPLIST Files;
    LPEDITFILE Current;
    I32 Insert;
} EDITCONTEXT, *LPEDITCONTEXT;

/***************************************************************************/

LPEDITLINE NewEditLine(I32 Size) {
    LPEDITLINE This = (LPEDITLINE)HeapAlloc(sizeof(EDITLINE));

    if (This == NULL) return NULL;

    This->Next = NULL;
    This->Prev = NULL;
    This->MaxChars = Size;
    This->NumChars = 0;
    This->Chars = (LPSTR)HeapAlloc(Size);

    return This;
}

/***************************************************************************/

void DeleteEditLine(LPEDITLINE This) {
    if (This == NULL) return;

    HeapFree(This->Chars);
    HeapFree(This);
}

/***************************************************************************/

void EditLineDestructor(LPVOID Item) { DeleteEditLine((LPEDITLINE)Item); }

/***************************************************************************/

LPEDITFILE NewEditFile(void) {
    LPEDITFILE This;
    LPEDITLINE Line;

    This = (LPEDITFILE)HeapAlloc(sizeof(EDITFILE));
    if (This == NULL) return NULL;

    This->Next = NULL;
    This->Prev = NULL;
    This->Lines = NewList(EditLineDestructor, NULL, NULL);
    This->Cursor.X = 0;
    This->Cursor.Y = 0;
    This->SelStart.X = 0;
    This->SelStart.Y = 0;
    This->SelEnd.X = 0;
    This->SelEnd.Y = 0;
    This->Left = 0;
    This->Top = 0;

    Line = NewEditLine(8);
    ListAddItem(This->Lines, Line);

    return This;
}

/***************************************************************************/

void DeleteEditFile(LPEDITFILE This) {
    if (This == NULL) return;

    DeleteList(This->Lines);
    HeapFree(This);
}

/***************************************************************************/

void EditFileDestructor(LPVOID Item) { DeleteEditFile((LPEDITFILE)Item); }

/***************************************************************************/

LPEDITCONTEXT NewEditContext(void) {
    LPEDITCONTEXT This = (LPEDITCONTEXT)HeapAlloc(sizeof(EDITCONTEXT));
    if (This == NULL) return NULL;

    This->Next = NULL;
    This->Prev = NULL;
    This->Files = NewList(EditFileDestructor, NULL, NULL);
    This->Current = NULL;
    This->Insert = 1;

    return This;
}

/***************************************************************************/

void DeleteEditContext(LPEDITCONTEXT This) {
    if (This == NULL) return;

    DeleteList(This->Files);
    HeapFree(This);
}

/***************************************************************************/

void CheckPositions(LPEDITFILE File) {
    I32 MinX = 0;
    I32 MinY = 0;
    I32 MaxX = MAX_COLUMNS;
    I32 MaxY = MAX_LINES;

    while (File->Cursor.X < MinX) {
        File->Left--;
        File->Cursor.X++;
    }
    while (File->Cursor.X >= MaxX) {
        File->Left++;
        File->Cursor.X--;
    }
    while (File->Cursor.Y < MinY) {
        File->Top--;
        File->Cursor.Y++;
    }
    while (File->Cursor.Y >= MaxY) {
        File->Top++;
        File->Cursor.Y--;
    }

    if (File->Left < 0) File->Left = 0;
    if (File->Top < 0) File->Top = 0;
}

/***************************************************************************/

void DrawText(LPEDITFILE File) {
    LPLISTNODE Node;
    LPEDITLINE Line;
    I32 Index;

    if (File == NULL) return;
    if (File->Lines->NumItems == 0) return;

    CheckPositions(File);

    for (Node = File->Lines->First, Index = 0; Node; Node = Node->Next) {
        if (Index == File->Top) break;
        Index++;
    }

    LockMutex(MUTEX_CONSOLE, INFINITY);

    Console.CursorX = 0;
    Console.CursorY = 0;

    for (; Node; Node = Node->Next) {
        Line = (LPEDITLINE)Node;
        if (File->Left < Line->NumChars) {
            for (Index = File->Left; Index < Line->NumChars; Index++) {
                if (Console.CursorX >= MAX_COLUMNS) break;
                ConsolePrintChar(Line->Chars[Index]);
            }
        }
        for (; Console.CursorX < MAX_COLUMNS;) ConsolePrintChar(STR_SPACE);
        Console.CursorX = 0;
        Console.CursorY++;
        if (Console.CursorY >= MAX_LINES) break;
    }

    for (; Console.CursorY < MAX_LINES; Console.CursorY++) {
        Console.CursorX = 0;
        for (Index = 0; Index < MAX_COLUMNS; Index++) {
            ConsolePrintChar(STR_SPACE);
        }
    }

    // Draw temp cursor

    Console.CursorX = File->Cursor.X;
    Console.CursorY = File->Cursor.Y;
    // ConsolePrintChar('_');
    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

static BOOL CheckLineSize(LPEDITLINE Line, I32 Size) {
    LPSTR Text;
    I32 NewSize;

    if (Size >= Line->MaxChars) {
        NewSize = ((Size / 8) + 1) * 8;
        Text = (LPSTR)HeapAlloc(NewSize);
        if (Text == NULL) return FALSE;
        StringCopyNum(Text, Line->Chars, Line->NumChars);
        HeapFree(Line->Chars);
        Line->MaxChars = NewSize;
        Line->Chars = Text;
    }

    return TRUE;
}

/***************************************************************************/

static void FillToCursor(LPEDITFILE File, LPEDITLINE Line) {
    I32 Index;
    I32 LineX;

    LineX = File->Left + File->Cursor.X;
    if (LineX <= Line->NumChars) return;
    if (CheckLineSize(Line, LineX) == FALSE) return;
    for (Index = Line->NumChars; Index < LineX; Index++) {
        Line->Chars[Index] = STR_SPACE;
    }
    Line->NumChars = LineX;
}

/***************************************************************************/

static LPEDITLINE GetCurrentLine(LPEDITFILE File) {
    I32 LineY = File->Top + File->Cursor.Y;
    return ListGetItem(File->Lines, LineY);
}

/***************************************************************************/

static void AddCharacter(LPEDITFILE File, STR ASCIICode) {
    LPEDITLINE Line;
    I32 Index;
    I32 LineX;
    I32 NewLength;

    LineX = File->Left + File->Cursor.X;
    Line = GetCurrentLine(File);
    if (Line == NULL) return;

    if (LineX <= Line->NumChars) {
        NewLength = Line->NumChars + 1;
    } else {
        NewLength = LineX;
    }

    // Resize the line if too small

    if (CheckLineSize(Line, NewLength) == FALSE) return;

    if (LineX > Line->NumChars) {
        FillToCursor(File, Line);
        Line->Chars[Line->NumChars++] = ASCIICode;
    } else {
        // Insert the character
        for (Index = Line->NumChars + 1; Index > LineX; Index--) {
            Line->Chars[Index] = Line->Chars[Index - 1];
        }
        Line->Chars[Index] = ASCIICode;
        Line->NumChars++;
    }

    // Update the cursor

    File->Cursor.X++;
    if (File->Cursor.X >= MAX_COLUMNS) {
        File->Left++;
        File->Cursor.X--;
    }
}

/***************************************************************************/

static void DeleteCharacter(LPEDITFILE File, I32 Flag) {
    LPLISTNODE Node;
    LPEDITLINE Line;
    LPEDITLINE NextLine;
    LPEDITLINE PrevLine;
    I32 LineX;
    I32 LineY;
    I32 NewLength;
    I32 Index;

    LineX = File->Left + File->Cursor.X;
    LineY = File->Top + File->Cursor.Y;
    Line = ListGetItem(File->Lines, LineY);
    if (Line == NULL) return;

    if (Flag == 0) {
        if (LineX > 0) {
            for (Index = LineX; Index < Line->NumChars; Index++) {
                Line->Chars[Index - 1] = Line->Chars[Index];
            }
            Line->NumChars--;
            File->Cursor.X--;
        } else {
            Node = (LPLISTNODE)Line;
            Node = Node->Prev;
            if (Node == NULL) return;
            PrevLine = (LPEDITLINE)Node;
            File->Cursor.X = PrevLine->NumChars;
            File->Cursor.Y--;
            NewLength = PrevLine->NumChars + Line->NumChars;
            if (CheckLineSize(PrevLine, NewLength) == FALSE) return;
            for (Index = 0; Index < Line->NumChars; Index++) {
                PrevLine->Chars[PrevLine->NumChars] = Line->Chars[Index];
                PrevLine->NumChars++;
            }
            ListEraseItem(File->Lines, Line);
        }
    } else {
        if (Line->NumChars == 0) {
            ListEraseItem(File->Lines, Line);
        } else {
            if (LineX >= Line->NumChars) {
                NextLine = ListGetItem(File->Lines, LineY + 1);
                if (NextLine == NULL) return;
                FillToCursor(File, Line);
                if (CheckLineSize(Line, Line->NumChars + NextLine->NumChars) == FALSE) return;
                for (Index = 0; Index < NextLine->NumChars; Index++) {
                    Line->Chars[Line->NumChars++] = NextLine->Chars[Index];
                }
                ListEraseItem(File->Lines, NextLine);
            } else {
                for (Index = LineX + 1; Index < Line->NumChars; Index++) {
                    Line->Chars[Index - 1] = Line->Chars[Index];
                }
                Line->NumChars--;
            }
        }
    }
}

/***************************************************************************/

static void AddLine(LPEDITFILE File) {
    LPEDITLINE Line;
    LPEDITLINE NewLine;
    LPLISTNODE Node;
    I32 LineX;
    I32 LineY;
    I32 Index;

    LineX = File->Left + File->Cursor.X;
    LineY = File->Top + File->Cursor.Y;
    Line = ListGetItem(File->Lines, LineY);
    if (Line == NULL) return;

    if (LineX == 0) {
        NewLine = NewEditLine(8);
        if (NewLine == NULL) return;

        Node = (LPLISTNODE)Line;
        if (Node == NULL || Node->Prev == NULL) {
            DeleteEditLine(NewLine);
            return;
        }

        ListAddAfter(File->Lines, Node->Prev, NewLine);

        File->Cursor.X = 0;
        File->Cursor.Y++;
    } else if (LineX >= Line->NumChars) {
        NewLine = NewEditLine(8);
        if (NewLine == NULL) return;

        Node = (LPLISTNODE)Line;
        if (Node == NULL) {
            DeleteEditLine(NewLine);
            return;
        }

        ListAddAfter(File->Lines, Node, NewLine);

        File->Cursor.X = 0;
        File->Cursor.Y++;
    } else {
        NewLine = NewEditLine(Line->NumChars);
        if (NewLine == NULL) return;

        for (Index = LineX; Index < Line->NumChars; Index++) {
            NewLine->Chars[Index - LineX] = Line->Chars[Index];
            NewLine->NumChars++;
        }

        Line->NumChars = LineX;

        Node = (LPLISTNODE)Line;
        if (Node == NULL) {
            DeleteEditLine(NewLine);
            return;
        }

        ListAddAfter(File->Lines, Node, NewLine);

        File->Cursor.X = 0;
        File->Cursor.Y++;
    }
}

/***************************************************************************/

static void GotoEndOfLine(LPEDITFILE File) {
    LPEDITLINE Line;

    Line = GetCurrentLine(File);
    if (Line == NULL) return;

    File->Cursor.X = Line->NumChars - File->Left;
}

/***************************************************************************/

static void GotoStartOfLine(LPEDITFILE File) {
    File->Left = 0;
    File->Cursor.X = 0;
}

/***************************************************************************/

static I32 Loop(LPEDITCONTEXT Context) {
    KEYCODE KeyCode;

    DrawText(Context->Current);

    while (1) {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            if (KeyCode.VirtualKey == VK_ESCAPE) {
                return 0;
            } else if (KeyCode.VirtualKey == VK_DOWN) {
                Context->Current->Cursor.Y++;
                DrawText(Context->Current);
            } else if (KeyCode.VirtualKey == VK_UP) {
                Context->Current->Cursor.Y--;
                DrawText(Context->Current);
            } else if (KeyCode.VirtualKey == VK_RIGHT) {
                Context->Current->Cursor.X++;
                DrawText(Context->Current);
            } else if (KeyCode.VirtualKey == VK_LEFT) {
                Context->Current->Cursor.X--;
                DrawText(Context->Current);
            } else if (KeyCode.VirtualKey == VK_HOME) {
                GotoStartOfLine(Context->Current);
                DrawText(Context->Current);
            } else if (KeyCode.VirtualKey == VK_END) {
                GotoEndOfLine(Context->Current);
                DrawText(Context->Current);
            } else if (KeyCode.VirtualKey == VK_BACKSPACE) {
                DeleteCharacter(Context->Current, 0);
                DrawText(Context->Current);
            } else if (KeyCode.VirtualKey == VK_DELETE) {
                DeleteCharacter(Context->Current, 1);
                DrawText(Context->Current);
            } else if (KeyCode.VirtualKey == VK_ENTER) {
                AddLine(Context->Current);
                DrawText(Context->Current);
            } else {
                switch (KeyCode.ASCIICode) {
                    default: {
                        if (KeyCode.ASCIICode >= STR_SPACE) {
                            AddCharacter(Context->Current, KeyCode.ASCIICode);
                            DrawText(Context->Current);
                        }
                    } break;
                }
            }
        }
    }

    return 0;
}

/***************************************************************************/

static BOOL OpenTextFile(LPEDITCONTEXT Context, LPCSTR Name) {
    FILEOPENINFO Info;
    FILEOPERATION FileOperation;
    LPEDITFILE File;
    LPEDITLINE Line;
    HANDLE Handle;
    LPSTR LineStart;
    LPSTR LineData;
    U8* Buffer;
    U32 FileSize;
    U32 LineSize;
    U32 FinalLineSize;
    U32 Index;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.Name = Name;
    Info.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    Handle = DoSystemCall(SYSCALL_OpenFile, (U32)&Info);

    if (Handle) {
        FileSize = DoSystemCall(SYSCALL_GetFileSize, Handle);
        if (FileSize) {
            Buffer = HeapAlloc(FileSize + 1);
            if (Buffer) {
                Buffer[FileSize] = STR_NULL;

                FileOperation.Header.Size = sizeof FileOperation;
                FileOperation.Header.Version = EXOS_ABI_VERSION;
                FileOperation.Header.Flags = 0;
                FileOperation.File = Handle;
                FileOperation.NumBytes = FileSize;
                FileOperation.Buffer = Buffer;

                if (DoSystemCall(SYSCALL_ReadFile, (U32)&FileOperation)) {
                    File = NewEditFile();
                    if (File) {
                        ListAddItem(Context->Files, File);
                        Context->Current = File;

                        LineData = (LPSTR)Buffer;
                        LineStart = LineData;
                        LineSize = 0;
                        FinalLineSize = 0;

                        while (*LineData) {
                            if (*LineData == 13 || *LineData == 10) {
                                Line = NewEditLine(FinalLineSize);
                                if (Line) {
                                    for (Index = 0; Index < LineSize; Index++) {
                                        if (LineStart[Index] == STR_TAB) {
                                            Line->Chars[Line->NumChars++] = STR_SPACE;
                                            Line->Chars[Line->NumChars++] = STR_SPACE;
                                            Line->Chars[Line->NumChars++] = STR_SPACE;
                                            Line->Chars[Line->NumChars++] = STR_SPACE;
                                        } else {
                                            Line->Chars[Line->NumChars++] = LineStart[Index];
                                        }
                                    }
                                    ListAddItem(File->Lines, Line);
                                }

                                LineData++;
                                while (*LineData == 10) LineData++;

                                LineStart = LineData;
                                LineSize = 0;
                                FinalLineSize = 0;
                            } else if (*LineData == STR_TAB) {
                                LineData++;
                                LineSize++;
                                FinalLineSize += 4;
                            } else {
                                LineData++;
                                LineSize++;
                                FinalLineSize++;
                            }
                        }
                    }
                }
                HeapFree(Buffer);
            }
        }
        DoSystemCall(SYSCALL_DeleteObject, Handle);
    } else {
        KernelLogText(LOG_VERBOSE, TEXT("Could not open file '%s'\n"), Name);
    }

    return TRUE;
}

/***************************************************************************/

U32 Edit(U32 NumArguments, LPCSTR* Arguments) {
    LPEDITCONTEXT Context;
    LPEDITFILE File;
    U32 Index;

    Context = NewEditContext();

    if (NumArguments) {
        for (Index = 0; Index < NumArguments; Index++) {
            OpenTextFile(Context, Arguments[Index]);
        }
    } else {
        File = NewEditFile();
        ListAddItem(Context->Files, File);
        Context->Current = File;
    }

    Loop(Context);

    DeleteEditContext(Context);

    return 0;
}

/***************************************************************************/
