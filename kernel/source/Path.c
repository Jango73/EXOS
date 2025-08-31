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


    Path utilities

\************************************************************************/
#include "../include/Path.h"

#include "../include/Heap.h"
#include "../include/List.h"
#include "../include/String.h"

static void PathComponentDestructor(LPVOID This) { HeapFree(This); }

/***************************************************************************/

LPLIST DecomposePath(LPCSTR Path) {
    STR Component[MAX_FILE_NAME];
    U32 PathIndex = 0;
    U32 ComponentIndex = 0;
    LPLIST List = NewList(PathComponentDestructor, HeapAlloc, HeapFree);
    LPPATHNODE Node = NULL;

    while (1) {
        ComponentIndex = 0;

        while (1) {
            if (Path[PathIndex] == STR_SLASH) {
                Component[ComponentIndex] = STR_NULL;
                PathIndex++;
                break;
            } else if (Path[PathIndex] == STR_NULL) {
                Component[ComponentIndex] = STR_NULL;
                break;
            } else {
                Component[ComponentIndex++] = Path[PathIndex++];
            }
        }

        Node = HeapAlloc(sizeof(PATHNODE));
        if (Node == NULL) goto Exit;
        StringCopy(Node->Name, Component);
        ListAddItem(List, Node);

        if (Path[PathIndex] == STR_NULL) break;
    }

Exit:

    return List;
}

/***************************************************************************/

static BOOL MatchStart(LPCSTR Name, LPCSTR Part) {
    U32 Index = 0;
    while (Part[Index] != STR_NULL) {
        if (CharToLower(Name[Index]) != CharToLower(Part[Index])) return FALSE;
        Index++;
    }
    return TRUE;
}

/***************************************************************************/

static void BuildMatches(LPPATHCOMPLETION Context, LPCSTR Path) {
    STR Dir[MAX_PATH_NAME];
    STR Part[MAX_FILE_NAME];
    STR Pattern[MAX_PATH_NAME];
    LPSTR Slash;
    FILEINFO Find;
    LPFILE File;

    Context->Matches.Count = 0;
    StringCopy(Context->Base, Path);
    Context->Index = 0;

    Slash = StringFindCharR(Path, PATH_SEP);
    if (Slash) {
        U32 DirectoryLength = Slash - Path + 1;
        StringCopyNum(Dir, Path, DirectoryLength);
        Dir[DirectoryLength] = STR_NULL;
        StringCopy(Part, Slash + 1);
    } else {
        Dir[0] = STR_NULL;
        StringCopy(Part, Path);
    }

    StringCopy(Pattern, Dir);
    StringConcat(Pattern, TEXT("*"));

    Find.Size = sizeof(FILEINFO);
    Find.FileSystem = Context->FileSystem;
    Find.Attributes = MAX_U32;
    StringCopy(Find.Name, Pattern);

    File = (LPFILE)Context->FileSystem->Driver->Command(DF_FS_OPENFILE, (U32)&Find);
    if (File == NULL) return;

    do {
        if (MatchStart(File->Name, Part)) {
            STR Full[MAX_PATH_NAME];
            StringCopy(Full, Dir);
            StringConcat(Full, File->Name);
            StringArrayAddUnique(&Context->Matches, Full);
        }
    } while (Context->FileSystem->Driver->Command(DF_FS_OPENNEXT, (U32)File) == DF_ERROR_SUCCESS);

    Context->FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32)File);
}

/***************************************************************************/

BOOL PathCompletionInit(LPPATHCOMPLETION Context, LPFILESYSTEM FileSystem) {
    Context->FileSystem = FileSystem;
    Context->Base[0] = STR_NULL;
    Context->Index = 0;
    return StringArrayInit(&Context->Matches, 32);
}

/***************************************************************************/

void PathCompletionDeinit(LPPATHCOMPLETION Context) { StringArrayDeinit(&Context->Matches); }

/***************************************************************************/

BOOL PathCompletionNext(LPPATHCOMPLETION Context, LPCSTR Path, LPSTR Output) {
    U32 Index;
    BOOL SameStart = TRUE;
    U32 BaseLength = StringLength(Context->Base);

    for (Index = 0; Index < BaseLength; Index++) {
        if (CharToLower(Path[Index]) != CharToLower(Context->Base[Index])) {
            SameStart = FALSE;
            break;
        }
    }

    if (Context->Matches.Count == 0 || SameStart == FALSE) {
        BuildMatches(Context, Path);
    } else {
        for (Index = 0; Index < Context->Matches.Count; Index++) {
            if (StringCompare(StringArrayGet(&Context->Matches, Index), Path) == 0) {
                Context->Index = Index + 1;
                if (Context->Index >= Context->Matches.Count) Context->Index = 0;
                break;
            }
        }
        if (Index == Context->Matches.Count) {
            BuildMatches(Context, Path);
        }
    }

    if (Context->Matches.Count == 0) return FALSE;

    StringCopy(Output, StringArrayGet(&Context->Matches, Context->Index));
    Context->Index++;
    if (Context->Index >= Context->Matches.Count) Context->Index = 0;

    return TRUE;
}

/***************************************************************************/
