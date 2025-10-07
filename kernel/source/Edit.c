
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

typedef BOOL (*EDITMENUPROC)(LPEDITCONTEXT);

typedef struct tag_EDITMENUITEM {
    KEYCODE Modifier;
    KEYCODE Key;
    LPCSTR Name;
    EDITMENUPROC Function;
} EDITMENUITEM, *LPEDITMENUITEM;

static BOOL CommandExit(LPEDITCONTEXT Context);
static BOOL CommandSave(LPEDITCONTEXT Context);

static EDITMENUITEM Menu[] = {
    {{VK_NONE, 0, 0}, {VK_ESCAPE, 0, 0}, TEXT("Exit"), CommandExit},
    {{VK_CONTROL, 0, 0}, {VK_S, 0, 0}, TEXT("Save"), CommandSave},
};
static const U32 MenuItems = sizeof(Menu) / sizeof(Menu[0]);

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

/**
 * @brief Draw the editor menu at the bottom of the console.
 */
static U16 ComposeConsoleAttribute(void) {
    U16 Attribute = (U16)(Console.ForeColor | (Console.BackColor << 0x04) | (Console.Blink << 0x07));
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

    LockMutex(MUTEX_CONSOLE, INFINITY);

    for (RowIndex = 0; RowIndex < MAX_LINES; RowIndex++) {
        U16* Row = Frame + (RowIndex * Width);

        for (Column = 0; Column < Width; Column++) {
            Row[Column] = SpaceCell;
        }

        if (Node) {
            Line = (LPEDITLINE)Node;
            if (File->Left < Line->NumChars) {
                I32 Start = File->Left;
                I32 End = Line->NumChars;
                I32 Visible = End - Start;

                if (Visible > MAX_COLUMNS) {
                    Visible = MAX_COLUMNS;
                }

                for (Index = 0; Index < Visible; Index++) {
                    Row[Index] = MakeConsoleCell(Line->Chars[Start + Index], Attribute);
                }
            }
            Node = Node->Next;
        }
    }

    RenderMenu(Attribute, SpaceCell);

    Console.CursorX = File->Cursor.X;
    Console.CursorY = File->Cursor.Y;
    SetConsoleCursorPosition(Console.CursorX, Console.Cursor.Y);

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
 * @brief Retrieve the line under the current cursor.
 * @param File File containing the line.
 * @return Pointer to the current line or NULL.
 */
static LPEDITLINE GetCurrentLine(LPEDITFILE File) {
    I32 LineY = File->Top + File->Cursor.Y;
    return ListGetItem(File->Lines, LineY);
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
    if (File->Cursor.X >= (I32)MAX_COLUMNS) {
        File->Left++;
        File->Cursor.X--;
    }
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

/**
 * @brief Move cursor to the end of current line.
 * @param File Active file.
 */
static void GotoEndOfLine(LPEDITFILE File) {
    LPEDITLINE Line;

    Line = GetCurrentLine(File);
    if (Line == NULL) return;

    File->Cursor.X = Line->NumChars - File->Left;
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

            if (KeyCode.VirtualKey == VK_DOWN) {
                Context->Current->Cursor.Y++;
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_UP) {
                Context->Current->Cursor.Y--;
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_RIGHT) {
                Context->Current->Cursor.X++;
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_LEFT) {
                Context->Current->Cursor.X--;
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_PAGEDOWN) {
                I32 Lines = (Console.Height * 8) / 10;
                Context->Current->Top += Lines;
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_PAGEUP) {
                I32 Lines = (Console.Height * 8) / 10;
                Context->Current->Top -= Lines;
                if (Context->Current->Top < 0) Context->Current->Top = 0;
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_HOME) {
                GotoStartOfLine(Context->Current);
                Render(Context->Current);
            } else if (KeyCode.VirtualKey == VK_END) {
                GotoEndOfLine(Context->Current);
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

                        LineData = (LPSTR)Buffer;
                        LineStart = LineData;
                        LineSize = 0;
                        FinalLineSize = 0;

                        while (*LineData) {
                            if (*LineData == 0x0D || *LineData == 0x0A) {
                                Line = NewEditLine(FinalLineSize ? FinalLineSize : 0x01);
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
