
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
#include "Mutex.h"
#include "drivers/Keyboard.h"
#include "List.h"
#include "Log.h"
#include "String.h"
#include "User.h"
#include "VKey.h"

/***************************************************************************/

typedef struct tag_EDITLINE EDITLINE, *LPEDITLINE;
typedef struct tag_EDITFILE EDITFILE, *LPEDITFILE;
typedef struct tag_EDITCONTEXT EDITCONTEXT, *LPEDITCONTEXT;

static const I32 TitleHeight = 1;
static I32 MenuHeight = 2;

#define MAX_COLUMNS (Console.Width - 10)
#define MAX_LINES (Console.Height - MenuHeight - TitleHeight)

#define EDIT_EOF_CHAR ((STR)0x1A)
#define EDIT_CLIPBOARD_NEWLINE ((STR)0x0A)

typedef BOOL (*EDITMENUPROC)(LPEDITCONTEXT);

typedef struct tag_EDITMENUITEM {
    KEYCODE Modifier;
    KEYCODE Key;
    LPCSTR Name;
    EDITMENUPROC Function;
} EDITMENUITEM, *LPEDITMENUITEM;

/***************************************************************************/

static BOOL CommandExit(LPEDITCONTEXT Context);
static BOOL CommandSave(LPEDITCONTEXT Context);
static BOOL CommandCut(LPEDITCONTEXT Context);
static BOOL CommandCopy(LPEDITCONTEXT Context);
static BOOL CommandPaste(LPEDITCONTEXT Context);
static BOOL CopySelectionToClipboard(LPEDITCONTEXT Context);
static void DeleteSelection(LPEDITFILE File);
static void AddCharacter(LPEDITFILE File, STR ASCIICode);
static void AddLine(LPEDITFILE File);

static EDITMENUITEM Menu[] = {
    {{VK_NONE, 0, 0}, {VK_ESCAPE, 0, 0}, TEXT("Exit"), CommandExit},
    {{VK_CONTROL, 0, 0}, {VK_S, 0, 0}, TEXT("Save"), CommandSave},
    {{VK_CONTROL, 0, 0}, {VK_X, 0, 0}, TEXT("Cut"), CommandCut},
    {{VK_CONTROL, 0, 0}, {VK_C, 0, 0}, TEXT("Copy"), CommandCopy},
    {{VK_CONTROL, 0, 0}, {VK_V, 0, 0}, TEXT("Paste"), CommandPaste},
};
static const U32 MenuItems = sizeof(Menu) / sizeof(Menu[0]);

static const KEYCODE ControlKey = {VK_CONTROL, 0, 0};
static const KEYCODE ShiftKey = {VK_SHIFT, 0, 0};

/***************************************************************************/

struct tag_EDITLINE {
    LISTNODE_FIELDS
    I32 MaxChars;
    I32 NumChars;
    LPSTR Chars;
};

/***************************************************************************/

struct tag_EDITFILE {
    LISTNODE_FIELDS
    LPLIST Lines;
    POINT Cursor;
    POINT SelStart;
    POINT SelEnd;
    I32 Left;
    I32 Top;
    LPSTR Name;
    BOOL Modified;
};

/***************************************************************************/

struct tag_EDITCONTEXT {
    LISTNODE_FIELDS
    LPLIST Files;
    LPEDITFILE Current;
    I32 Insert;
    LPSTR Clipboard;
    I32 ClipboardSize;
    BOOL ShowLineNumbers;
};

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
    This->Modified = FALSE;

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
    This->ShowLineNumbers = FALSE;

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
static void ConsoleFill(U32 Row, U32 Column, U32 Length);
static void RenderTitleBar(LPEDITFILE File, U32 ForeColor, U32 BackColor, U32 Width);
static void RenderMenu(U32 ForeColor, U32 BackColor, U32 Width);

/***************************************************************************/

/**
 * @brief Render the current file content to the console.
 * @param Context Editor context providing rendering settings.
 */
static void Render(LPEDITCONTEXT Context) {
    LPLISTNODE Node;
    LPEDITLINE Line;
    I32 Index;
    U32 RowIndex;
    LPEDITFILE File;
    BOOL ShowLineNumbers;
    U32 TextColumnOffset;
    U32 Width;
    BOOL PendingEofMarker = FALSE;
    BOOL EofDrawn = FALSE;
    BOOL HasSelection;
    POINT SelectionStart;
    POINT SelectionEnd;
    U32 DefaultForeColor;
    U32 DefaultBackColor;
    U32 MenuForeColor = CONSOLE_WHITE;
    U32 MenuBackColor = CONSOLE_BLUE;
    U32 TitleForeColor = CONSOLE_WHITE;
    U32 TitleBackColor = CONSOLE_BLUE;
    U32 LineNumberForeColor = CONSOLE_BLACK;
    U32 LineNumberBackColor = CONSOLE_WHITE;
    U32 SelectionForeColor;
    U32 SelectionBackColor;

    if (Context == NULL) return;

    File = Context->Current;

    if (File == NULL) return;
    if (File->Lines->NumItems == 0) return;

    ShowLineNumbers = Context->ShowLineNumbers;
    TextColumnOffset = ShowLineNumbers ? 4U : 0U;

    CheckPositions(File);

    for (Node = File->Lines->First, Index = 0; Node; Node = Node->Next) {
        if (Index == File->Top) break;
        Index++;
    }

    Width = Console.Width;
    DefaultForeColor = Console.ForeColor;
    DefaultBackColor = Console.BackColor;
    SelectionForeColor = DefaultBackColor;
    SelectionBackColor = DefaultForeColor;

    HasSelection = SelectionHasRange(File);
    if (HasSelection) {
        NormalizeSelection(File, &SelectionStart, &SelectionEnd);
    }

    LockMutex(MUTEX_CONSOLE, INFINITY);

    RenderTitleBar(File, TitleForeColor, TitleBackColor, Width);

    for (RowIndex = 0; RowIndex < MAX_LINES; RowIndex++) {
        LPEDITLINE CurrentLine = NULL;
        I32 LineLength = 0;
        I32 AbsoluteRow = File->Top + (I32)RowIndex;
        U32 TargetRow = TitleHeight + RowIndex;
        BOOL RowHasEofMarker = FALSE;

        SetConsoleForeColor(DefaultForeColor);
        SetConsoleBackColor(DefaultBackColor);
        ConsoleFill(TargetRow, 0, Width);

        if (ShowLineNumbers) {
            SetConsoleForeColor(LineNumberForeColor);
            SetConsoleBackColor(LineNumberBackColor);
            ConsoleFill(TargetRow, 0, TextColumnOffset);
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

                I32 MaxVisible = (I32)Width - (I32)TextColumnOffset;
                if (MaxVisible < 0) MaxVisible = 0;
                if (Visible > MaxVisible) {
                    Visible = MaxVisible;
                }

                if (Visible > 0) {
                    SetConsoleForeColor(DefaultForeColor);
                    SetConsoleBackColor(DefaultBackColor);
                    ConsolePrintLine(TargetRow, TextColumnOffset, &Line->Chars[Start], (U32)Visible);
                }
                LineLength = Line->NumChars;
            } else {
                LineLength = Line->NumChars;
            }

            if (CurrentNode->Next == NULL) {
                PendingEofMarker = TRUE;
            }

            Node = CurrentNode->Next;

            if (ShowLineNumbers && CurrentLine) {
                STR LineNumberText[8];
                U32 DigitCount;

                SetConsoleForeColor(LineNumberForeColor);
                SetConsoleBackColor(LineNumberBackColor);
                StringPrintFormat(LineNumberText, TEXT("%3d"), AbsoluteRow + 1);
                DigitCount = StringLength(LineNumberText);
                if (DigitCount > TextColumnOffset) {
                    DigitCount = TextColumnOffset;
                }
                if (DigitCount > Width) {
                    DigitCount = Width;
                }
                if (DigitCount > 0) {
                    ConsolePrintLine(TargetRow, 0, LineNumberText, DigitCount);
                }
            }
        } else {
            if (PendingEofMarker && EofDrawn == FALSE) {
                U32 TargetColumn = TextColumnOffset;
                if (TargetColumn < Width) {
                    STR EofChar[1];
                    EofChar[0] = EDIT_EOF_CHAR;
                    SetConsoleForeColor(DefaultForeColor);
                    SetConsoleBackColor(DefaultBackColor);
                    ConsolePrintLine(TargetRow, TargetColumn, EofChar, 1);
                    RowHasEofMarker = TRUE;
                }
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

                I32 MaxVisible = (I32)Width - (I32)TextColumnOffset;
                if (MaxVisible < 0) MaxVisible = 0;
                if (VisibleEnd > MaxVisible) VisibleEnd = MaxVisible;

                if (VisibleStart < VisibleEnd) {
                    U32 HighlightColumn = TextColumnOffset + (U32)VisibleStart;
                    U32 HighlightLength = (U32)(VisibleEnd - VisibleStart);

                    if (HighlightColumn < Width) {
                        if (HighlightLength > (Width - HighlightColumn)) {
                            HighlightLength = Width - HighlightColumn;
                        }

                        if (HighlightLength > 0) {
                            U32 Remaining = HighlightLength;
                            I32 SourceIndex = RangeStart;
                            U32 BufferOffset = 0;

                            while (Remaining > 0) {
                                STR HighlightBuffer[64];
                                U32 Chunk = Remaining;
                                U32 IndexInChunk;

                                if (Chunk > (U32)(sizeof(HighlightBuffer) / sizeof(HighlightBuffer[0]))) {
                                    Chunk = (U32)(sizeof(HighlightBuffer) / sizeof(HighlightBuffer[0]));
                                }

                                for (IndexInChunk = 0; IndexInChunk < Chunk; IndexInChunk++) {
                                    STR Character = STR_SPACE;

                                    if (CurrentLine && (SourceIndex + (I32)IndexInChunk) < CurrentLine->NumChars) {
                                        Character = CurrentLine->Chars[SourceIndex + (I32)IndexInChunk];
                                    } else if (RowHasEofMarker && HighlightColumn == TextColumnOffset && IndexInChunk == 0) {
                                        Character = EDIT_EOF_CHAR;
                                    }

                                    HighlightBuffer[IndexInChunk] = Character;
                                }

                                SetConsoleForeColor(SelectionForeColor);
                                SetConsoleBackColor(SelectionBackColor);
                                ConsolePrintLine(TargetRow, HighlightColumn + BufferOffset, HighlightBuffer, Chunk);

                                BufferOffset += Chunk;
                                SourceIndex += (I32)Chunk;
                                Remaining -= Chunk;
                            }

                            SetConsoleForeColor(DefaultForeColor);
                            SetConsoleBackColor(DefaultBackColor);
                        }
                    }
                }
            }
        }
    }

    RenderMenu(MenuForeColor, MenuBackColor, Width);

    Console.CursorX = (I32)TextColumnOffset + File->Cursor.X;
    if (Console.CursorX >= (I32)Width) {
        Console.CursorX = (I32)Width - 1;
    }
    Console.CursorY = TitleHeight + File->Cursor.Y;
    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);

    SetConsoleForeColor(DefaultForeColor);
    SetConsoleBackColor(DefaultBackColor);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

static void ConsoleFill(U32 Row, U32 Column, U32 Length) {
    STR SpaceBuffer[32];
    U32 Index;

    for (Index = 0; Index < (U32)(sizeof(SpaceBuffer) / sizeof(SpaceBuffer[0])); Index++) {
        SpaceBuffer[Index] = STR_SPACE;
    }

    while (Length > 0) {
        U32 Chunk = Length;

        if (Chunk > (U32)(sizeof(SpaceBuffer) / sizeof(SpaceBuffer[0]))) {
            Chunk = (U32)(sizeof(SpaceBuffer) / sizeof(SpaceBuffer[0]));
        }

        ConsolePrintLine(Row, Column, SpaceBuffer, Chunk);

        Column += Chunk;
        Length -= Chunk;
    }
}

/***************************************************************************/

static void PrintMenuChar(U32 Row, U32* Column, STR Character, U32 Width) {
    STR Buffer[1];

    if (Column == NULL) return;
    if (*Column >= Width) return;

    Buffer[0] = Character;
    ConsolePrintLine(Row, *Column, Buffer, 1);
    (*Column)++;
}

/***************************************************************************/

static void PrintMenuText(U32 Row, U32* Column, LPCSTR Text, U32 Width) {
    U32 Length;
    U32 Remaining;

    if (Column == NULL || Text == NULL) return;
    if (*Column >= Width) return;

    Remaining = Width - *Column;
    Length = StringLength(Text);
    if (Length > Remaining) {
        Length = Remaining;
    }

    if (Length == 0) return;

    ConsolePrintLine(Row, *Column, Text, Length);
    *Column += Length;
}

/***************************************************************************/

static void RenderTitleBar(LPEDITFILE File, U32 ForeColor, U32 BackColor, U32 Width) {
    I32 Line;
    U32 Column = 0;
    LPCSTR Name;

    if (TitleHeight <= 0) return;

    SetConsoleForeColor(ForeColor);
    SetConsoleBackColor(BackColor);

    for (Line = 0; Line < TitleHeight; Line++) {
        ConsoleFill((U32)Line, 0, Width);
    }

    if (File && File->Modified && Column < Width) {
        STR Modified = '*';
        ConsolePrintLine(0, Column, &Modified, 1);
        Column++;
    }

    if (File && File->Name) {
        Name = File->Name;
    } else {
        Name = TEXT("<untitled>");
    }

    if (Name && Column < Width) {
        U32 NameLength = StringLength(Name);
        if (NameLength > (Width - Column)) {
            NameLength = Width - Column;
        }
        ConsolePrintLine(0, Column, Name, NameLength);
    }
}

/***************************************************************************/

static void RenderMenu(U32 ForeColor, U32 BackColor, U32 Width) {
    U32 Item;
    I32 Line;
    U32 Column;
    U32 MenuRow = TitleHeight + MAX_LINES;

    SetConsoleForeColor(ForeColor);
    SetConsoleBackColor(BackColor);

    for (Line = 0; Line < MenuHeight; Line++) {
        ConsoleFill((U32)(TitleHeight + MAX_LINES + Line), 0, Width);
    }

    Column = 0;

    for (Item = 0; Item < MenuItems && Column < Width; Item++) {
        LPEDITMENUITEM MenuItem = &Menu[Item];
        LPCSTR ModifierName = NULL;
        LPCSTR KeyName = NULL;

        if (MenuItem->Modifier.VirtualKey != VK_NONE) {
            ModifierName = GetKeyName(MenuItem->Modifier.VirtualKey);
            PrintMenuText(MenuRow, &Column, ModifierName, Width);
            PrintMenuChar(MenuRow, &Column, '+', Width);
        }

        KeyName = GetKeyName(MenuItem->Key.VirtualKey);
        PrintMenuText(MenuRow, &Column, KeyName, Width);
        PrintMenuChar(MenuRow, &Column, ' ', Width);

        PrintMenuText(MenuRow, &Column, MenuItem->Name, Width);
        PrintMenuChar(MenuRow, &Column, ' ', Width);
        PrintMenuChar(MenuRow, &Column, ' ', Width);
    }
}

/***************************************************************************/

/**
 * @brief Draw the editor menu at the bottom of the console.
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

        File->Modified = FALSE;
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

static BOOL CommandCut(LPEDITCONTEXT Context) {
    LPEDITFILE File;
    LPEDITLINE Line;
    LPLISTNODE Node;
    BOOL HasNextLine;
    POINT CursorPosition;
    I32 LineY;
    I32 Length;
    LPSTR Buffer = NULL;
    I32 Index;
    BOOL Modified = FALSE;

    if (Context == NULL) return FALSE;

    File = Context->Current;
    if (File == NULL) return FALSE;

    if (SelectionHasRange(File)) {
        if (CopySelectionToClipboard(Context)) {
            DeleteSelection(File);
        }
        return FALSE;
    }

    CursorPosition = GetAbsoluteCursor(File);
    LineY = CursorPosition.Y;

    Line = ListGetItem(File->Lines, LineY);
    if (Line == NULL) return FALSE;

    Node = (LPLISTNODE)Line;
    HasNextLine = (Node != NULL && Node->Next != NULL);

    Length = Line->NumChars;
    if (HasNextLine) {
        Length++;
    }

    if (Length > 0) {
        Buffer = (LPSTR)HeapAlloc(Length);
        if (Buffer == NULL) return FALSE;

        for (Index = 0; Index < Line->NumChars; Index++) {
            Buffer[Index] = Line->Chars[Index];
        }

        if (HasNextLine) {
            Buffer[Line->NumChars] = EDIT_CLIPBOARD_NEWLINE;
        }
    }

    HeapFree(Context->Clipboard);
    Context->Clipboard = Buffer;
    Context->ClipboardSize = Length;

    if (HasNextLine) {
        File->SelStart.X = 0;
        File->SelStart.Y = LineY;
        File->SelEnd.X = 0;
        File->SelEnd.Y = LineY + 1;
        DeleteSelection(File);
        CollapseSelectionToCursor(File);
        return FALSE;
    }

    if (File->Lines->NumItems > 1) {
        LPLISTNODE PrevNode = Node ? Node->Prev : NULL;

        ListEraseItem(File->Lines, Line);
        Modified = TRUE;

        if (PrevNode) {
            MoveCursorToAbsolute(File, 0, LineY - 1);
        } else {
            MoveCursorToAbsolute(File, 0, 0);
        }
    } else {
        Line->NumChars = 0;
        MoveCursorToAbsolute(File, 0, LineY);
        if (Length > 0) {
            Modified = TRUE;
        }
    }

    if (Modified) {
        File->Modified = TRUE;
    }

    return FALSE;
}

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
    BOOL Modified = FALSE;

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
        Modified = TRUE;
    } else {
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
        Modified = TRUE;

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

    if (Modified) {
        File->Modified = TRUE;
    }
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
    File->Modified = TRUE;
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
    BOOL Modified = FALSE;

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
            Modified = TRUE;
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
            Modified = TRUE;
        }
    } else {
        if (Line->NumChars == 0) {
            ListEraseItem(File->Lines, Line);
            Modified = TRUE;
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
                Modified = TRUE;
            } else {
                for (Index = LineX + 1; Index < Line->NumChars; Index++) {
                    Line->Chars[Index - 1] = Line->Chars[Index];
                }
                Line->NumChars--;
                Modified = TRUE;
            }
        }
    }
    CollapseSelectionToCursor(File);
    if (Modified) {
        File->Modified = TRUE;
    }
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
    BOOL Modified = FALSE;

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
        Modified = TRUE;
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
        Modified = TRUE;
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
        Modified = TRUE;
    }

    CollapseSelectionToCursor(File);
    if (Modified) {
        File->Modified = TRUE;
    }
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

    Render(Context);

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
                        Render(Context);
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
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_UP) {
                Context->Current->Cursor.Y--;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_RIGHT) {
                Context->Current->Cursor.X++;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_LEFT) {
                Context->Current->Cursor.X--;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_PAGEDOWN) {
                I32 Lines = (Console.Height * 8) / 10;
                Context->Current->Top += Lines;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_PAGEUP) {
                I32 Lines = (Console.Height * 8) / 10;
                Context->Current->Top -= Lines;
                if (Context->Current->Top < 0) Context->Current->Top = 0;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_HOME) {
                if (GetKeyCodeDown(ControlKey)) {
                    GotoStartOfFile(Context->Current);
                } else {
                    GotoStartOfLine(Context->Current);
                }
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_END) {
                if (GetKeyCodeDown(ControlKey)) {
                    GotoEndOfFile(Context->Current);
                } else {
                    GotoEndOfLine(Context->Current);
                }
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_BACKSPACE) {
                DeleteCharacter(Context->Current, 0);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_DELETE) {
                DeleteCharacter(Context->Current, 1);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_ENTER) {
                AddLine(Context->Current);
                Render(Context);
            } else {
                switch (KeyCode.ASCIICode) {
                    default: {
                        if (KeyCode.ASCIICode >= STR_SPACE) {
                            AddCharacter(Context->Current, KeyCode.ASCIICode);
                            Render(Context);
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
        if (File) {
            File->Modified = FALSE;
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
 * @param LineNumbers TRUE to enable line numbers display.
 * @return 0 on success or error code.
 */
U32 Edit(U32 NumArguments, LPCSTR* Arguments, BOOL LineNumbers) {
    LPEDITCONTEXT Context;
    LPEDITFILE File;
    U32 Index;

    Context = NewEditContext();

    if (Context == NULL) {
        return DF_ERROR_GENERIC;
    }

    Context->ShowLineNumbers = LineNumbers;

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
