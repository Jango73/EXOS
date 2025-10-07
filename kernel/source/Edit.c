
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


    Edit

\************************************************************************/

#include "Base.h"
#include "Console.h"
#include "Heap.h"
#include "Kernel.h"
#include "drivers/Keyboard.h"
#include "List.h"
#include "Log.h"
#include "User.h"
#include "VKey.h"

/***************************************************************************/

typedef struct tag_EDITCONTEXT EDITCONTEXT, *LPEDITCONTEXT;

static I32 MenuHeight = 2;

#define MAX_COLUMNS (Console.Width - 10)
#define MAX_LINES (Console.Height - MenuHeight)

#define EDIT_EOF_CHAR ((STR)0x1A)
#define EDIT_CLIPBOARD_NEWLINE ((STR)0x0A)

typedef BOOL (*EDITMENUPROC)(LPEDITCONTEXT);

typedef struct tag_EDITMENUITEM {
    KEYCODE Modifier;
    KEYCODE Key;
    LPCSTR Name;
    EDITMENUPROC Function;
} EDITMENUITEM, *LPEDITMENUITEM;

static BOOL CommandExit(LPEDITCONTEXT Context);
static BOOL CommandSave(LPEDITCONTEXT Context);
static BOOL CommandCopy(LPEDITCONTEXT Context);
static BOOL CommandPaste(LPEDITCONTEXT Context);
static BOOL CopySelectionToClipboard(LPEDITCONTEXT Context);
static void AddCharacter(LPEDITFILE File, STR ASCIICode);
static void AddLine(LPEDITFILE File);

static EDITMENUITEM Menu[] = {
    {{VK_NONE, 0, 0}, {VK_ESCAPE, 0, 0}, TEXT("Exit"), CommandExit},
    {{VK_CONTROL, 0, 0}, {VK_S, 0, 0}, TEXT("Save"), CommandSave},
    {{VK_CONTROL, 0, 0}, {VK_C, 0, 0}, TEXT("Copy"), CommandCopy},
    {{VK_CONTROL, 0, 0}, {VK_V, 0, 0}, TEXT("Paste"), CommandPaste},
};
static const U32 MenuItems = sizeof(Menu) / sizeof(Menu[0]);

static const KEYCODE ControlKey = {VK_CONTROL, 0, 0};
static const KEYCODE ShiftKey = {VK_SHIFT, 0, 0};

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
    LPSTR Name;
} EDITFILE, *LPEDITFILE;

/***************************************************************************/

typedef struct tag_EDITCONTEXT {
    LISTNODE_FIELDS
    LPLIST Files;
    LPEDITFILE Current;
    I32 Insert;
    LPSTR Clipboard;
    I32 ClipboardSize;
} EDITCONTEXT, *LPEDITCONTEXT;

/**
 * @brief Allocate a new editable line with a given capacity.
 * @param Size Maximum number of characters in the line.
 * @return Pointer to the newly created line or NULL on failure.
 */
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

/**
 * @brief Free an editable line and its resources.
 * @param This Line to destroy.
 */
void DeleteEditLine(LPEDITLINE This) {
    if (This == NULL) return;

    HeapFree(This->Chars);
    HeapFree(This);
}

/***************************************************************************/

/**
 * @brief List destructor callback for edit lines.
 * @param Item Item to delete.
 */
void EditLineDestructor(LPVOID Item) { DeleteEditLine((LPEDITLINE)Item); }

/***************************************************************************/

/**
 * @brief Create a new editable file instance.
 * @return Pointer to a new EDITFILE or NULL on failure.
 */
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
    This->Name = NULL;

    Line = NewEditLine(8);
    ListAddItem(This->Lines, Line);

    return This;
}

/***************************************************************************/

/**
 * @brief Destroy an editable file and all contained lines.
 * @param This File to delete.
 */
void DeleteEditFile(LPEDITFILE This) {
    if (This == NULL) return;

    DeleteList(This->Lines);
    HeapFree(This->Name);
    HeapFree(This);
}

/***************************************************************************/

/**
 * @brief List destructor callback for edit files.
 * @param Item Item to delete.
 */
void EditFileDestructor(LPVOID Item) { DeleteEditFile((LPEDITFILE)Item); }

/***************************************************************************/

/**
 * @brief Allocate a new editor context.
 * @return Pointer to a new EDITCONTEXT or NULL on failure.
 */
LPEDITCONTEXT NewEditContext(void) {
    LPEDITCONTEXT This = (LPEDITCONTEXT)HeapAlloc(sizeof(EDITCONTEXT));
    if (This == NULL) return NULL;

    This->Next = NULL;
    This->Prev = NULL;
    This->Files = NewList(EditFileDestructor, NULL, NULL);
    This->Current = NULL;
    This->Insert = 1;
    This->Clipboard = NULL;
    This->ClipboardSize = 0;

    return This;
}

/***************************************************************************/

/**
 * @brief Destroy an editor context and its files list.
 * @param This Context to delete.
 */
void DeleteEditContext(LPEDITCONTEXT This) {
    if (This == NULL) return;

    DeleteList(This->Files);
    HeapFree(This->Clipboard);
    HeapFree(This);
}

/***************************************************************************/

/**
 * @brief Ensure cursor and viewport positions remain within bounds.
 * @param File File whose positions are validated.
 */
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

static POINT GetAbsoluteCursor(const LPEDITFILE File) {
    POINT Position;

    Position.X = 0;
    Position.Y = 0;
    if (File == NULL) return Position;

    Position.X = File->Left + File->Cursor.X;
    Position.Y = File->Top + File->Cursor.Y;

    return Position;
}

/***************************************************************************/

static BOOL SelectionHasRange(const LPEDITFILE File) {
    if (File == NULL) return FALSE;
    return (File->SelStart.X != File->SelEnd.X) || (File->SelStart.Y != File->SelEnd.Y);
}

/***************************************************************************/

static void NormalizeSelection(const LPEDITFILE File, POINT* Start, POINT* End) {
    POINT Temp;

    if (File == NULL || Start == NULL || End == NULL) return;

    *Start = File->SelStart;
    *End = File->SelEnd;

    if ((Start->Y > End->Y) || (Start->Y == End->Y && Start->X > End->X)) {
        Temp = *Start;
        *Start = *End;
        *End = Temp;
    }
}

/***************************************************************************/

static void CollapseSelectionToCursor(LPEDITFILE File) {
    POINT Position;

    if (File == NULL) return;

    Position = GetAbsoluteCursor(File);
    File->SelStart = Position;
    File->SelEnd = Position;
}

/***************************************************************************/

static void UpdateSelectionAfterMove(LPEDITFILE File, BOOL Extend, POINT Previous) {
    if (File == NULL) return;

    if (Extend) {
        if (SelectionHasRange(File) == FALSE) {
            File->SelStart = Previous;
        }
        File->SelEnd = GetAbsoluteCursor(File);
    } else {
        CollapseSelectionToCursor(File);
    }
}

/***************************************************************************/

static void MoveCursorToAbsolute(LPEDITFILE File, I32 Column, I32 Line) {
    if (File == NULL) return;

    if (Line < 0) Line = 0;
    if (Column < 0) Column = 0;

    if (Line < File->Top) {
        File->Top = Line;
    } else if (Line >= (File->Top + MAX_LINES)) {
        File->Top = Line - (MAX_LINES - 1);
        if (File->Top < 0) File->Top = 0;
    }

    if (Column < File->Left) {
        File->Left = Column;
    } else if (Column >= (File->Left + MAX_COLUMNS)) {
        File->Left = Column - (MAX_COLUMNS - 1);
        if (File->Left < 0) File->Left = 0;
    }

    File->Cursor.Y = Line - File->Top;
    File->Cursor.X = Column - File->Left;
    if (File->Cursor.Y < 0) File->Cursor.Y = 0;
    if (File->Cursor.X < 0) File->Cursor.X = 0;

    CollapseSelectionToCursor(File);
}

/***************************************************************************/

/**
 * @brief Draw the editor menu at the bottom of the console.
 */
static U16 ComposeConsoleAttribute(void) {
    U16 Attribute = (U16)(Console.ForeColor | (Console.BackColor << 0x04) | (Console.Blink << 0x07));
    return (U16)(Attribute << 0x08);
}

/***************************************************************************/

static U16 ComposeInverseConsoleAttribute(void) {
    U16 Attribute = (U16)(Console.BackColor | (Console.ForeColor << 0x04) | (Console.Blink << 0x07));
    return (U16)(Attribute << 0x08);
}

/***************************************************************************/

static inline U16 MakeConsoleCell(STR Char, U16 Attribute) { return (U16)Char | Attribute; }

/***************************************************************************/

static void WriteMenuText(U16* Row, U32 Width, U32* Column, LPCSTR Text, U16 Attribute) {
    if (Row == NULL || Text == NULL || Column == NULL) return;

    while (*Text && *Column < Width) {
        Row[*Column] = MakeConsoleCell(*Text, Attribute);
        (*Column)++;
        Text++;
    }
}

/***************************************************************************/

static void RenderMenu(U16 Attribute, U16 SpaceCell) {
    U32 Item;
    I32 Line;
    U32 Column;
    U32 Width = Console.Width;
    U16* Frame = Console.Memory;
    U16* Row;

    for (Line = 0; Line < MenuHeight; Line++) {
        Row = Frame + ((MAX_LINES + Line) * Width);
        for (Column = 0; Column < Width; Column++) {
            Row[Column] = SpaceCell;
        }
    }

    Row = Frame + (MAX_LINES * Width);
    Column = 0;

    for (Item = 0; Item < MenuItems && Column < Width; Item++) {
        if (Menu[Item].Modifier.VirtualKey != VK_NONE) {
            WriteMenuText(Row, Width, &Column, GetKeyName(Menu[Item].Modifier.VirtualKey), Attribute);
            if (Column < Width) {
                Row[Column++] = MakeConsoleCell('+', Attribute);
            }
        }

        WriteMenuText(Row, Width, &Column, GetKeyName(Menu[Item].Key.VirtualKey), Attribute);
        if (Column < Width) {
            Row[Column++] = SpaceCell;
        }

        WriteMenuText(Row, Width, &Column, Menu[Item].Name, Attribute);
        if (Column < Width) {
            Row[Column++] = SpaceCell;
        }
        if (Column < Width) {
            Row[Column++] = SpaceCell;
        }
    }
}

/***************************************************************************/

/**
 * @brief Render the current file content to the console.
 * @param File File to render.
 */
void Render(LPEDITFILE File) {
    LPLISTNODE Node;
    LPEDITLINE Line;
    I32 Index;
    U32 RowIndex;
    U32 Column;
    U16 Attribute;
    U16 SpaceCell;
    U32 Width;
    U16* Frame;
    BOOL PendingEofMarker = FALSE;
    BOOL EofDrawn = FALSE;

    if (File == NULL) return;
    if (File->Lines->NumItems == 0) return;

    CheckPositions(File);

    for (Node = File->Lines->First, Index = 0; Node; Node = Node->Next) {
        if (Index == File->Top) break;
        Index++;
    }

    Attribute = ComposeConsoleAttribute();
    SpaceCell = MakeConsoleCell(STR_SPACE, Attribute);
    Width = Console.Width;
    Frame = Console.Memory;

    U16 SelectedAttribute = ComposeInverseConsoleAttribute();
    BOOL HasSelection = SelectionHasRange(File);
    POINT SelectionStart;
    POINT SelectionEnd;

    if (HasSelection) {
        NormalizeSelection(File, &SelectionStart, &SelectionEnd);
    }

    LockMutex(MUTEX_CONSOLE, INFINITY);

    for (RowIndex = 0; RowIndex < MAX_LINES; RowIndex++) {
        U16* Row = Frame + (RowIndex * Width);
        LPEDITLINE CurrentLine = NULL;
        I32 LineLength = 0;
        I32 AbsoluteRow = File->Top + (I32)RowIndex;

        for (Column = 0; Column < Width; Column++) {
            Row[Column] = SpaceCell;
        }

        if (Node) {
            LPLISTNODE CurrentNode = Node;
            I32 Start = File->Left;
            I32 Visible = 0;

            Line = (LPEDITLINE)CurrentNode;
            CurrentLine = Line;

            if (Start < 0) Start = 0;

            if (Start < Line->NumChars) {
                I32 End = Line->NumChars;

                Visible = End - Start;
                if (Visible > MAX_COLUMNS) {
                    Visible = MAX_COLUMNS;
                }

                for (Index = 0; Index < Visible && Index < (I32)Width; Index++) {
                    Row[Index] = MakeConsoleCell(Line->Chars[Start + Index], Attribute);
                }
                LineLength = Line->NumChars;
            } else {
                Visible = 0;
                LineLength = Line->NumChars;
            }

            if (CurrentNode->Next == NULL) {
                PendingEofMarker = TRUE;
            }

            Node = CurrentNode->Next;
        } else {
            if (PendingEofMarker && EofDrawn == FALSE) {
                Row[0] = MakeConsoleCell(EDIT_EOF_CHAR, Attribute);
                EofDrawn = TRUE;
                PendingEofMarker = FALSE;
            }
        }

        if (HasSelection) {
            I32 RangeStart = 0;
            I32 RangeEnd = 0;

            if (AbsoluteRow < SelectionStart.Y || AbsoluteRow > SelectionEnd.Y) {
                RangeStart = 0;
                RangeEnd = 0;
            } else if (SelectionStart.Y == SelectionEnd.Y) {
                RangeStart = SelectionStart.X;
                RangeEnd = SelectionEnd.X;
            } else if (AbsoluteRow == SelectionStart.Y) {
                RangeStart = SelectionStart.X;
                RangeEnd = LineLength;
            } else if (AbsoluteRow == SelectionEnd.Y) {
                RangeStart = 0;
                RangeEnd = SelectionEnd.X;
            } else {
                RangeStart = 0;
                RangeEnd = LineLength;
            }

            if (RangeStart < 0) RangeStart = 0;
            if (RangeEnd < RangeStart) RangeEnd = RangeStart;

            if (CurrentLine) {
                if (RangeStart > CurrentLine->NumChars) RangeStart = CurrentLine->NumChars;
                if (RangeEnd > CurrentLine->NumChars) RangeEnd = CurrentLine->NumChars;
            } else {
                RangeStart = 0;
                if (RangeEnd < 0) RangeEnd = 0;
            }

            if (AbsoluteRow == SelectionEnd.Y && AbsoluteRow > SelectionStart.Y && SelectionEnd.X == 0) {
                RangeEnd = RangeStart + 1;
            }

            if (RangeEnd > RangeStart) {
                I32 VisibleStart = RangeStart - File->Left;
                I32 VisibleEnd = RangeEnd - File->Left;

                if (VisibleStart < 0) VisibleStart = 0;
                if (VisibleEnd < 0) VisibleEnd = 0;
                if (VisibleEnd > (I32)Width) VisibleEnd = (I32)Width;

                if (VisibleStart < VisibleEnd) {
                    for (Index = VisibleStart; Index < VisibleEnd; Index++) {
                        STR Character = (STR)(Row[Index] & 0x00FF);
                        Row[Index] = MakeConsoleCell(Character, SelectedAttribute);
                    }
                }
            }
        }
    }

    RenderMenu(Attribute, SpaceCell);

    Console.CursorX = File->Cursor.X;
    Console.CursorY = File->Cursor.Y;
    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

/**
 * @brief Handler for the exit command.
 * @param Context Active editor context.
 * @return TRUE to terminate the editor.
 */
static BOOL CommandExit(LPEDITCONTEXT Context) {
    UNUSED(Context);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Save the current file to disk.
 * @param File File to save.
 * @return TRUE on success, FALSE on error.
 */
static BOOL SaveFile(LPEDITFILE File) {
    FILEOPENINFO Info;
    FILEOPERATION Operation;
    LPLISTNODE Node;
    LPEDITLINE Line;
    HANDLE Handle;
    U8 CRLF[2] = {13, 10};

    if (File == NULL || File->Name == NULL) return FALSE;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.Name = File->Name;
    Info.Flags = FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;

    Handle = DoSystemCall(SYSCALL_OpenFile, (U32)&Info);
    if (Handle) {
        LPEDITLINE LastContentLine = NULL;

        for (Node = File->Lines->Last; Node; Node = Node->Prev) {
            Line = (LPEDITLINE)Node;
            if (Line->NumChars > 0) {
                LastContentLine = Line;
                break;
            }
        }

        if (LastContentLine) {
            for (Node = File->Lines->First; Node; Node = Node->Next) {
                Line = (LPEDITLINE)Node;

                Operation.Header.Size = sizeof Operation;
                Operation.Header.Version = EXOS_ABI_VERSION;
                Operation.Header.Flags = 0;
                Operation.File = Handle;
                Operation.Buffer = Line->Chars;
                Operation.NumBytes = Line->NumChars;
                DoSystemCall(SYSCALL_WriteFile, (U32)&Operation);

                Operation.Buffer = CRLF;
                Operation.NumBytes = 2;
                DoSystemCall(SYSCALL_WriteFile, (U32)&Operation);

                if (Line == LastContentLine) {
                    break;
                }
            }
        }

        DoSystemCall(SYSCALL_DeleteObject, Handle);
    } else {
        KernelLogText(LOG_VERBOSE, TEXT("Could not save file '%s'\n"), File->Name);
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Handler for the save command.
 * @param Context Active editor context.
 * @return TRUE if the file was saved.
 */
static BOOL CommandSave(LPEDITCONTEXT Context) { return SaveFile(Context->Current); }

/***************************************************************************/

static BOOL CommandCopy(LPEDITCONTEXT Context) {
    if (Context == NULL) return FALSE;
    CopySelectionToClipboard(Context);
    return FALSE;
}

/***************************************************************************/

static BOOL CommandPaste(LPEDITCONTEXT Context) {
    LPEDITFILE File;
    I32 Index;

    if (Context == NULL) return FALSE;

    File = Context->Current;
    if (File == NULL) return FALSE;
    if (Context->Clipboard == NULL) return FALSE;
    if (Context->ClipboardSize <= 0) return FALSE;

    for (Index = 0; Index < Context->ClipboardSize; Index++) {
        STR Character = Context->Clipboard[Index];
        if (Character == EDIT_CLIPBOARD_NEWLINE) {
            AddLine(File);
        } else {
            AddCharacter(File, Character);
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Ensure a line has enough capacity for a given index.
 * @param Line Line to check.
 * @param Size Desired capacity.
 * @return TRUE on success, FALSE if memory allocation failed.
 */
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

/**
 * @brief Append characters from a buffer to an edit line expanding tabs.
 * @param Line Destination line.
 * @param Data Source buffer.
 * @param Length Number of characters to append from the buffer.
 */
static void AppendBufferToLine(LPEDITLINE Line, LPCSTR Data, U32 Length) {
    U32 Index;

    if (Line == NULL || Data == NULL) return;

    for (Index = 0; Index < Length; Index++) {
        if (Data[Index] == STR_TAB) {
            if (CheckLineSize(Line, Line->NumChars + 0x04) == FALSE) return;
            Line->Chars[Line->NumChars++] = STR_SPACE;
            Line->Chars[Line->NumChars++] = STR_SPACE;
            Line->Chars[Line->NumChars++] = STR_SPACE;
            Line->Chars[Line->NumChars++] = STR_SPACE;
        } else {
            if (CheckLineSize(Line, Line->NumChars + 0x01) == FALSE) return;
            Line->Chars[Line->NumChars++] = Data[Index];
        }
    }
}

/***************************************************************************/

/**
 * @brief Fill the current line with spaces up to the cursor position.
 * @param File Active file.
 * @param Line Line to modify.
 */
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

/**
 * @brief Ensure the file contains a line at the requested index.
 * @param File Active file.
 * @param LineIndex Zero-based line index to retrieve.
 * @return Pointer to the ensured line or NULL on failure.
 */
static LPEDITLINE EnsureLineAt(LPEDITFILE File, I32 LineIndex) {
    LPEDITLINE Line;

    if (File == NULL) return NULL;
    if (LineIndex < 0) return NULL;

    while ((I32)File->Lines->NumItems <= LineIndex) {
        Line = NewEditLine(8);
        if (Line == NULL) return NULL;
        if (ListAddItem(File->Lines, Line) == FALSE) {
            DeleteEditLine(Line);
            return NULL;
        }
    }

    return (LPEDITLINE)ListGetItem(File->Lines, LineIndex);
}

/***************************************************************************/

/**
 * @brief Retrieve the line under the current cursor.
 * @param File File containing the line.
 * @return Pointer to the current line or NULL.
 */
static LPEDITLINE GetCurrentLine(LPEDITFILE File) {
    I32 LineY = File->Top + File->Cursor.Y;
    return ListGetItem(File->Lines, LineY);
}

/***************************************************************************/

static void DeleteSelection(LPEDITFILE File) {
    POINT Start;
    POINT End;
    LPEDITLINE StartLine;
    LPEDITLINE EndLine;
    I32 StartColumn;
    I32 EndColumn;
    I32 TailLength;
    I32 LineIndex;
    I32 Remaining;
    I32 Offset;

    if (File == NULL) return;
    if (SelectionHasRange(File) == FALSE) return;

    NormalizeSelection(File, &Start, &End);

    StartLine = ListGetItem(File->Lines, Start.Y);
    if (StartLine == NULL) return;

    if (Start.Y == End.Y) {
        StartColumn = Start.X;
        EndColumn = End.X;

        if (StartColumn > StartLine->NumChars) StartColumn = StartLine->NumChars;
        if (EndColumn > StartLine->NumChars) EndColumn = StartLine->NumChars;
        if (EndColumn <= StartColumn) {
            MoveCursorToAbsolute(File, StartColumn, Start.Y);
            return;
        }

        Remaining = StartLine->NumChars - EndColumn;
        for (Offset = 0; Offset < Remaining; Offset++) {
            StartLine->Chars[StartColumn + Offset] = StartLine->Chars[EndColumn + Offset];
        }
        StartLine->NumChars -= (EndColumn - StartColumn);

        MoveCursorToAbsolute(File, StartColumn, Start.Y);
        return;
    }

    StartColumn = Start.X;
    if (StartColumn > StartLine->NumChars) StartColumn = StartLine->NumChars;

    EndLine = ListGetItem(File->Lines, End.Y);
    EndColumn = End.X;
    TailLength = 0;

    if (EndLine) {
        if (EndColumn > EndLine->NumChars) EndColumn = EndLine->NumChars;
        if (EndColumn < 0) EndColumn = 0;
        TailLength = EndLine->NumChars - EndColumn;
        if (TailLength < 0) TailLength = 0;
        if (TailLength > 0) {
            if (CheckLineSize(StartLine, StartColumn + TailLength) == FALSE) return;
        }
    }

    StartLine->NumChars = StartColumn;

    for (LineIndex = End.Y - 1; LineIndex > Start.Y; LineIndex--) {
        LPEDITLINE MiddleLine = ListGetItem(File->Lines, LineIndex);
        if (MiddleLine) {
            ListEraseItem(File->Lines, MiddleLine);
        }
    }

    if (EndLine && EndLine != StartLine) {
        for (Offset = 0; Offset < TailLength; Offset++) {
            StartLine->Chars[StartLine->NumChars++] = EndLine->Chars[EndColumn + Offset];
        }
        ListEraseItem(File->Lines, EndLine);
    }

    MoveCursorToAbsolute(File, StartColumn, Start.Y);
}

/***************************************************************************/

static BOOL CopySelectionToClipboard(LPEDITCONTEXT Context) {
    LPEDITFILE File;
    POINT Start;
    POINT End;
    I32 LineIndex;
    I32 SegmentStart;
    I32 SegmentEnd;
    I32 Length;
    I32 Position;
    LPEDITLINE Line;
    LPSTR Buffer;

    if (Context == NULL) return FALSE;

    File = Context->Current;
    if (File == NULL) return FALSE;
    if (SelectionHasRange(File) == FALSE) return FALSE;

    NormalizeSelection(File, &Start, &End);

    Length = 0;

    for (LineIndex = Start.Y; LineIndex <= End.Y; LineIndex++) {
        Line = ListGetItem(File->Lines, LineIndex);
        if (Line == NULL) break;

        SegmentStart = 0;
        SegmentEnd = Line->NumChars;

        if (LineIndex == Start.Y) {
            SegmentStart = Start.X;
            if (SegmentStart > Line->NumChars) SegmentStart = Line->NumChars;
        }

        if (LineIndex == End.Y) {
            SegmentEnd = End.X;
            if (SegmentEnd > Line->NumChars) SegmentEnd = Line->NumChars;
        }

        if (LineIndex == Start.Y && LineIndex != End.Y) {
            SegmentEnd = Line->NumChars;
        }

        if (SegmentEnd < SegmentStart) SegmentEnd = SegmentStart;

        Length += (SegmentEnd - SegmentStart);

        if (LineIndex < End.Y) {
            Length++;
        }
    }

    if (Length <= 0) return FALSE;

    Buffer = (LPSTR)HeapAlloc(Length);
    if (Buffer == NULL) return FALSE;

    Position = 0;

    for (LineIndex = Start.Y; LineIndex <= End.Y && Position < Length; LineIndex++) {
        Line = ListGetItem(File->Lines, LineIndex);
        if (Line == NULL) break;

        SegmentStart = 0;
        SegmentEnd = Line->NumChars;

        if (LineIndex == Start.Y) {
            SegmentStart = Start.X;
            if (SegmentStart > Line->NumChars) SegmentStart = Line->NumChars;
        }

        if (LineIndex == End.Y) {
            SegmentEnd = End.X;
            if (SegmentEnd > Line->NumChars) SegmentEnd = Line->NumChars;
        }

        if (LineIndex == Start.Y && LineIndex != End.Y) {
            SegmentEnd = Line->NumChars;
        }

        if (SegmentEnd < SegmentStart) SegmentEnd = SegmentStart;

        for (; SegmentStart < SegmentEnd && Position < Length; SegmentStart++) {
            Buffer[Position++] = Line->Chars[SegmentStart];
        }

        if (LineIndex < End.Y && Position < Length) {
            Buffer[Position++] = EDIT_CLIPBOARD_NEWLINE;
        }
    }

    HeapFree(Context->Clipboard);
    Context->Clipboard = Buffer;
    Context->ClipboardSize = Position;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Insert a character at the cursor position.
 * @param File Active file.
 * @param ASCIICode Character to insert.
 */
static void AddCharacter(LPEDITFILE File, STR ASCIICode) {
    LPEDITLINE Line;
    I32 Index;
    I32 LineX;
    I32 NewLength;
    I32 LineY;

    if (File == NULL) return;

    if (SelectionHasRange(File)) {
        DeleteSelection(File);
    }

    LineX = File->Left + File->Cursor.X;
    LineY = File->Top + File->Cursor.Y;
    if (LineY < 0) LineY = 0;

    Line = EnsureLineAt(File, LineY);
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
    if (File->Cursor.X >= (I32)MAX_COLUMNS) {
        File->Left++;
        File->Cursor.X--;
    }
    CollapseSelectionToCursor(File);
}

/***************************************************************************/

/**
 * @brief Remove a character relative to the cursor.
 * @param File Active file.
 * @param Flag 0 deletes before cursor, non-zero deletes at cursor.
 */
static void DeleteCharacter(LPEDITFILE File, I32 Flag) {
    LPLISTNODE Node;
    LPEDITLINE Line;
    LPEDITLINE NextLine;
    LPEDITLINE PrevLine;
    I32 LineX;
    I32 LineY;
    I32 NewLength;
    I32 Index;

    if (File == NULL) return;

    if (SelectionHasRange(File)) {
        DeleteSelection(File);
        return;
    }

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
    CollapseSelectionToCursor(File);
}

/***************************************************************************/

/**
 * @brief Split the current line at the cursor position.
 * @param File Active file.
 */
static void AddLine(LPEDITFILE File) {
    LPEDITLINE Line;
    LPEDITLINE NewLine;
    LPLISTNODE Node;
    I32 LineX;
    I32 LineY;
    I32 Index;

    if (File == NULL) return;

    if (SelectionHasRange(File)) {
        DeleteSelection(File);
    }

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

    CollapseSelectionToCursor(File);
}

/***************************************************************************/

/**
 * @brief Move cursor to the end of current line.
 * @param File Active file.
 */
static void GotoEndOfLine(LPEDITFILE File) {
    LPEDITLINE Line;
    I32 TargetColumn;
    I32 MaxVisible;
    I32 LineIndex;

    if (File == NULL) return;

    LineIndex = File->Top + File->Cursor.Y;
    if (LineIndex < 0) LineIndex = 0;

    Line = ListGetItem(File->Lines, LineIndex);
    if (Line == NULL) {
        File->Left = 0;
        File->Cursor.X = 0;
        return;
    }

    TargetColumn = Line->NumChars;
    if (TargetColumn <= 0) {
        File->Left = 0;
        File->Cursor.X = 0;
        return;
    }

    MaxVisible = MAX_COLUMNS;
    if (MaxVisible < 1) MaxVisible = 1;

    if (TargetColumn <= MaxVisible) {
        File->Left = 0;
    } else if (TargetColumn < File->Left) {
        File->Left = TargetColumn;
    }

    if ((TargetColumn - File->Left) >= MaxVisible) {
        File->Left = TargetColumn - (MaxVisible - 1);
    }

    if (File->Left < 0) File->Left = 0;

    File->Cursor.X = TargetColumn - File->Left;
    if (File->Cursor.X < 0) File->Cursor.X = 0;
    if (File->Cursor.X > MaxVisible) File->Cursor.X = MaxVisible;
}

/***************************************************************************/

/**
 * @brief Move cursor to the beginning of the file.
 * @param File Active file.
 */
static void GotoStartOfFile(LPEDITFILE File) {
    if (File == NULL) return;

    File->Left = 0;
    File->Top = 0;
    File->Cursor.X = 0;
    File->Cursor.Y = 0;
}

/***************************************************************************/

/**
 * @brief Move cursor to the start of current line.
 * @param File Active file.
 */
static void GotoStartOfLine(LPEDITFILE File) {
    File->Left = 0;
    File->Cursor.X = 0;
}

/***************************************************************************/

/**
 * @brief Move cursor to the end of the file.
 * @param File Active file.
 */
static void GotoEndOfFile(LPEDITFILE File) {
    I32 LastLineIndex;
    I32 VisibleRows;

    if (File == NULL || File->Lines == NULL) return;

    if (File->Lines->NumItems == 0) {
        File->Left = 0;
        File->Top = 0;
        File->Cursor.X = 0;
        File->Cursor.Y = 0;
        return;
    }

    LastLineIndex = (I32)File->Lines->NumItems - 1;
    VisibleRows = MAX_LINES;
    if (VisibleRows < 1) VisibleRows = 1;

    if (LastLineIndex < VisibleRows) {
        File->Top = 0;
        File->Cursor.Y = LastLineIndex;
    } else {
        File->Top = LastLineIndex - (VisibleRows - 1);
        if (File->Top < 0) File->Top = 0;
        File->Cursor.Y = LastLineIndex - File->Top;
        if (File->Cursor.Y >= VisibleRows) {
            File->Cursor.Y = VisibleRows - 1;
        }
    }

    File->Left = 0;
    GotoEndOfLine(File);
}

/***************************************************************************/

/**
 * @brief Main editor loop handling user input.
 * @param Context Editor context.
 * @return Exit code.
 */
static I32 Loop(LPEDITCONTEXT Context) {
    KEYCODE KeyCode;
    U32 Item;
    BOOL Handled;

    Render(Context->Current);

    FOREVER {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            Handled = FALSE;
            for (Item = 0; Item < MenuItems; Item++) {
                if (KeyCode.VirtualKey == Menu[Item].Key.VirtualKey) {
                    if (Menu[Item].Modifier.VirtualKey == VK_NONE || GetKeyCodeDown(Menu[Item].Modifier)) {
                        Handled = TRUE;
                        if (Menu[Item].Function(Context)) {
                            return 0;
                        }
                        Render(Context->Current);
                    }
                    break;
                }
            }

            if (Handled) continue;

            if (Context->Current == NULL) continue;

            BOOL ShiftDown = GetKeyCodeDown(ShiftKey);
            POINT PreviousPosition = GetAbsoluteCursor(Context->Current);

            if (KeyCode.VirtualKey == VK_DOWN) {
                Context->Current->Cursor.Y++;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_UP) {
                Context->Current->Cursor.Y--;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_RIGHT) {
                Context->Current->Cursor.X++;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_LEFT) {
                Context->Current->Cursor.X--;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_PAGEDOWN) {
                I32 Lines = (Console.Height * 8) / 10;
                Context->Current->Top += Lines;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_PAGEUP) {
                I32 Lines = (Console.Height * 8) / 10;
                Context->Current->Top -= Lines;
                if (Context->Current->Top < 0) Context->Current->Top = 0;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_HOME) {
                if (GetKeyCodeDown(ControlKey)) {
                    GotoStartOfFile(Context->Current);
                } else {
                    GotoStartOfLine(Context->Current);
                }
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_END) {
                if (GetKeyCodeDown(ControlKey)) {
                    GotoEndOfFile(Context->Current);
                } else {
                    GotoEndOfLine(Context->Current);
                }
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_BACKSPACE) {
                DeleteCharacter(Context->Current, 0);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_DELETE) {
                DeleteCharacter(Context->Current, 1);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_ENTER) {
                AddLine(Context->Current);
                Render(Context->Current);
            } else {
                switch (KeyCode.ASCIICode) {
                    default: {
                        if (KeyCode.ASCIICode >= STR_SPACE) {
                            AddCharacter(Context->Current, KeyCode.ASCIICode);
                            Render(Context->Current);
                        }
                    } break;
                }
            }
        }

        Sleep(20);
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Load a text file into the editor.
 * @param Context Editor context.
 * @param Name Path of the file to open.
 * @return TRUE on success, FALSE on error.
 */
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
                        File->Name = HeapAlloc(StringLength(Name) + 1);
                        if (File->Name) {
                            StringCopy(File->Name, Name);
                        }
                        ListAddItem(Context->Files, File);
                        Context->Current = File;

                        ListReset(File->Lines);

                        LineData = (LPSTR)Buffer;
                        LineStart = LineData;
                        LineSize = 0;
                        FinalLineSize = 0;

                        while (*LineData) {
                            if (*LineData == 0x0D || *LineData == 0x0A) {
                                Line = NewEditLine(FinalLineSize ? FinalLineSize : 0x01);
                                if (Line) {
                                    AppendBufferToLine(Line, LineStart, LineSize);
                                    ListAddItem(File->Lines, Line);
                                }

                                if (*LineData == 0x0D && LineData[1] == 0x0A) {
                                    LineData += 0x02;
                                } else {
                                    LineData++;
                                }

                                LineStart = LineData;
                                LineSize = 0x00;
                                FinalLineSize = 0x00;
                            } else if (*LineData == STR_TAB) {
                                LineData++;
                                LineSize++;
                                FinalLineSize += 0x04;
                            } else {
                                LineData++;
                                LineSize++;
                                FinalLineSize++;
                            }
                        }

                        if (LineSize > 0 || File->Lines->NumItems == 0) {
                            Line = NewEditLine(FinalLineSize ? FinalLineSize : 0x01);
                            if (Line) {
                                AppendBufferToLine(Line, LineStart, LineSize);
                                ListAddItem(File->Lines, Line);
                            }
                        }
                    }
                }
                HeapFree(Buffer);
            }
        }
        DoSystemCall(SYSCALL_DeleteObject, Handle);
    } else {
        File = NewEditFile();
        if (File) {
            if (Name) {
                File->Name = HeapAlloc(StringLength(Name) + 1);
                if (File->Name) {
                    StringCopy(File->Name, Name);
                }
            }
            ListAddItem(Context->Files, File);
            Context->Current = File;
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Entry point for the text editor utility.
 * @param NumArguments Number of command line arguments.
 * @param Arguments Array of argument strings.
 * @return 0 on success or error code.
 */
U32 Edit(U32 NumArguments, LPCSTR* Arguments) {
    LPEDITCONTEXT Context;
    LPEDITFILE File;
    U32 Index;

    Context = NewEditContext();

    if (NumArguments && Arguments) {
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
    ClearConsole();

    return 0;
}
