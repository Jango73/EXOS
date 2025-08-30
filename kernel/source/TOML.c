
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


    TOML

\************************************************************************/
// TOML.c

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/TOML.h"

#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/String.h"

/***************************************************************************/

LPTOML TomlParse(LPCSTR Source) {
    LPTOML Toml = NULL;
    LPTOMLITEM Last = NULL;
    STR Section[0x80];
    STR SectionBase[0x80];
    U32 SectionIndex = 0;
    U32 Index = 0;

    KernelLogText(LOG_DEBUG, TEXT("[TomlParse] Enter"));

    Toml = (LPTOML)HeapAlloc(sizeof(TOML));
    if (Toml == NULL) return NULL;
    Toml->First = NULL;

    Section[0] = STR_NULL;
    SectionBase[0] = STR_NULL;
    if (Source == NULL) return Toml;

    while (Source[Index]) {
        STR Line[0x100];
        U32 LineLen = 0;
        LPSTR Ptr = NULL;
        LPSTR Comment = NULL;
        LPSTR Equal = NULL;
        LPSTR Key = NULL;
        LPSTR Value = NULL;
        LPSTR End = NULL;
        STR FullKey[0x100];
        LPTOMLITEM Item = NULL;

        while (Source[Index] && Source[Index] != '\n') {
            if (LineLen < 0xFF) {
                Line[LineLen++] = Source[Index];
            }
            Index++;
        }

        if (Source[Index] == '\n') Index++;
        Line[LineLen] = STR_NULL;

        Comment = StringFindChar(Line, '#');
        if (Comment) *Comment = STR_NULL;

        Ptr = Line;
        while (*Ptr == ' ' || *Ptr == '\t') Ptr++;
        if (*Ptr == STR_NULL) continue;

        if (*Ptr == '[') {
            BOOL Array = FALSE;

            Ptr++;
            if (*Ptr == '[') {
                Array = TRUE;
                Ptr++;
            }
            End = StringFindChar(Ptr, ']');
            if (End) {
                *End = STR_NULL;
                if (Array && End[1] == ']') End++;
            }

            if (Array) {
                if (StringCompare(SectionBase, Ptr) == 0) {
                    SectionIndex++;
                } else {
                    StringCopy(SectionBase, Ptr);
                    SectionIndex = 0;
                }
                STR IndexText[0x10];
                U32ToString(SectionIndex, IndexText);
                StringCopy(Section, SectionBase);
                StringConcat(Section, (LPCSTR) ".");
                StringConcat(Section, IndexText);
            } else {
                StringCopy(Section, Ptr);
                SectionBase[0] = STR_NULL;
                SectionIndex = 0;
            }
            continue;
        }

        Equal = StringFindChar(Ptr, '=');
        if (Equal == NULL) continue;
        *Equal = STR_NULL;
        Key = Ptr;
        Value = Equal + 1;

        End = Key + StringLength(Key);
        while (End > Key && (End[-1] == ' ' || End[-1] == '\t')) {
            End[-1] = STR_NULL;
            End--;
        }

        while (*Value == ' ' || *Value == '\t') Value++;
        End = Value + StringLength(Value);
        while (End > Value && (End[-1] == ' ' || End[-1] == '\t' || End[-1] == '\r')) {
            End[-1] = STR_NULL;
            End--;
        }

        if (*Value == '\"') {
            Value++;
            End = StringFindChar(Value, '\"');
            if (End) *End = STR_NULL;
        }

        FullKey[0] = STR_NULL;
        if (!StringEmpty(Section)) {
            StringCopy(FullKey, Section);
            StringConcat(FullKey, (LPCSTR) ".");
        }
        StringConcat(FullKey, Key);

        Item = (LPTOMLITEM)HeapAlloc(sizeof(TOMLITEM));
        if (Item == NULL) continue;
        Item->Next = NULL;
        Item->Key = (LPSTR)HeapAlloc(StringLength(FullKey) + 1);
        Item->Value = (LPSTR)HeapAlloc(StringLength(Value) + 1);
        if (Item->Key == NULL || Item->Value == NULL) {
            if (Item->Key) HeapFree(Item->Key);
            if (Item->Value) HeapFree(Item->Value);
            HeapFree(Item);
            continue;
        }
        StringCopy(Item->Key, FullKey);
        StringCopy(Item->Value, Value);

        if (Toml->First == NULL) {
            Toml->First = Item;
        } else {
            Last->Next = Item;
        }
        Last = Item;
    }

    KernelLogText(LOG_DEBUG, TEXT("[TomlParse] Exit"));

    return Toml;
}

/***************************************************************************/

LPCSTR TomlGet(LPTOML Toml, LPCSTR Path) {
    LPTOMLITEM Item = NULL;

    KernelLogText(LOG_DEBUG, TEXT("[TomlGet] Enter"));

    if (Toml == NULL) return NULL;
    if (Path == NULL) return NULL;

    for (Item = Toml->First; Item; Item = Item->Next) {
        if (StringCompare(Item->Key, Path) == 0) {
            KernelLogText(LOG_DEBUG, TEXT("[TomlGet] Exit"));
            return Item->Value;
        }
    }

    KernelLogText(LOG_DEBUG, TEXT("[TomlGet] Exit"));

    return NULL;
}

/***************************************************************************/

void TomlFree(LPTOML Toml) {
    LPTOMLITEM Item = NULL;
    LPTOMLITEM Next = NULL;

    KernelLogText(LOG_DEBUG, TEXT("[TomlFree] Enter"));

    if (Toml == NULL) return;

    for (Item = Toml->First; Item; Item = Next) {
        Next = Item->Next;
        if (Item->Key) HeapFree(Item->Key);
        if (Item->Value) HeapFree(Item->Value);
        HeapFree(Item);
    }

    HeapFree(Toml);

    KernelLogText(LOG_DEBUG, TEXT("[TomlFree] Exit"));
}

/***************************************************************************/
