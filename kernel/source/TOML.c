// TOML.c

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/TOML.h"

#include "../include/Heap.h"
#include "../include/String.h"

/***************************************************************************/

LPTOML TomlParse(LPCSTR Source) {
    LPTOML Toml = NULL;
    LPTOMLITEM Last = NULL;
    STR Section[0x80];
    U32 Index = 0;

    Toml = (LPTOML)HeapAlloc(sizeof(TOML));
    if (Toml == NULL) return NULL;
    Toml->First = NULL;

    Section[0] = STR_NULL;
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
            Ptr++;
            End = StringFindChar(Ptr, ']');
            if (End) *End = STR_NULL;
            StringCopy(Section, Ptr);
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
            StringConcat(FullKey, ".");
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

    return Toml;
}

/***************************************************************************/

LPCSTR TomlGet(LPTOML Toml, LPCSTR Path) {
    LPTOMLITEM Item = NULL;

    if (Toml == NULL) return NULL;
    if (Path == NULL) return NULL;

    for (Item = Toml->First; Item; Item = Item->Next) {
        if (StringCompare(Item->Key, Path) == 0) {
            return Item->Value;
        }
    }

    return NULL;
}

/***************************************************************************/

void TomlFree(LPTOML Toml) {
    LPTOMLITEM Item = NULL;
    LPTOMLITEM Next = NULL;

    if (Toml == NULL) return;

    for (Item = Toml->First; Item; Item = Next) {
        Next = Item->Next;
        if (Item->Key) HeapFree(Item->Key);
        if (Item->Value) HeapFree(Item->Value);
        HeapFree(Item);
    }

    HeapFree(Toml);
}

/***************************************************************************/
