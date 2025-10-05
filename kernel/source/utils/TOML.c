
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

#include "utils/TOML.h"

#include "Kernel.h"
#include "Log.h"
#include "String.h"

/***************************************************************************/

/**
 * @brief Parses a TOML formatted string into a structured data object.
 *
 * This function implements a basic TOML parser that supports sections,
 * key-value pairs, strings, and arrays. It builds a linked list of
 * TOMLITEM structures representing the parsed configuration.
 *
 * @param Source TOML-formatted string to parse
 * @return Pointer to TOML structure containing parsed data, or NULL on allocation error
 */
LPTOML TomlParse(LPCSTR Source) {
    LPTOML Toml = NULL;
    LPTOMLITEM Last = NULL;
    STR Section[0x80];
    STR SectionBase[0x80];
    U32 SectionIndex = 0;
    U32 Index = 0;

    DEBUG(TEXT("[TomlParse] Enter"));

    // Allocate main TOML structure
    Toml = (LPTOML)KernelHeapAlloc(sizeof(TOML));
    if (Toml == NULL) return NULL;
    Toml->First = NULL;

    // Initialize section tracking variables
    Section[0] = STR_NULL;      // Current full section name (e.g., "server.database")
    SectionBase[0] = STR_NULL;  // Base section name (e.g., "server")
    if (Source == NULL) return Toml;

    // Main parsing loop - process each line of the TOML source
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

        // Extract current line from source
        while (Source[Index] && Source[Index] != '\n') {
            if (LineLen < 0xFF) {
                Line[LineLen++] = Source[Index];
            }
            Index++;
        }

        if (Source[Index] == '\n') Index++;
        Line[LineLen] = STR_NULL;

        // Remove comments (everything after '#')
        Comment = StringFindChar(Line, '#');
        if (Comment) *Comment = STR_NULL;

        // Skip leading whitespace
        Ptr = Line;
        while (*Ptr == ' ' || *Ptr == '\t') Ptr++;
        if (*Ptr == STR_NULL) continue;  // Skip empty lines

        // Handle section headers: [section] or [[array]]
        if (*Ptr == '[') {
            BOOL Array = FALSE;

            Ptr++;  // Skip first '['
            if (*Ptr == '[') {
                Array = TRUE;  // This is an array of tables [[section]]
                Ptr++;         // Skip second '['
            }

            // Find closing bracket(s)
            End = StringFindChar(Ptr, ']');
            if (End) {
                *End = STR_NULL;
                if (Array && End[1] == ']') End++;  // Skip second ']' for arrays
            }

            if (Array) {
                // Handle array of tables: [[servers]] creates servers.0, servers.1, etc.
                if (STRINGS_EQUAL(SectionBase, Ptr)) {
                    SectionIndex++;  // Same section, increment index
                } else {
                    StringCopy(SectionBase, Ptr);  // New section
                    SectionIndex = 0;
                }

                // Build section name like "servers.0", "servers.1"
                STR IndexText[0x10];
                U32ToString(SectionIndex, IndexText);
                StringCopy(Section, SectionBase);
                StringConcat(Section, TEXT("."));
                StringConcat(Section, IndexText);
            } else {
                // Regular section: [section.subsection]
                StringCopy(Section, Ptr);
                SectionBase[0] = STR_NULL;
                SectionIndex = 0;
            }
            continue;  // Move to next line
        }

        // Handle key-value pairs: key = value
        Equal = StringFindChar(Ptr, '=');
        if (Equal == NULL) continue;  // Skip lines without '='

        *Equal = STR_NULL;  // Split the line at '='
        Key = Ptr;
        Value = Equal + 1;

        // Trim trailing whitespace from key
        End = Key + StringLength(Key);
        while (End > Key && (End[-1] == ' ' || End[-1] == '\t')) {
            End[-1] = STR_NULL;
            End--;
        }

        // Trim leading and trailing whitespace from value
        while (*Value == ' ' || *Value == '\t') Value++;
        End = Value + StringLength(Value);
        while (End > Value && (End[-1] == ' ' || End[-1] == '\t' || End[-1] == '\r')) {
            End[-1] = STR_NULL;
            End--;
        }

        // Handle string values in quotes: "value"
        if (*Value == '\"') {
            Value++;  // Skip opening quote
            End = StringFindChar(Value, '\"');
            if (End) *End = STR_NULL;  // Remove closing quote
        }

        // Build full key name combining section and key: "section.key"
        FullKey[0] = STR_NULL;
        if (!StringEmpty(Section)) {
            StringCopy(FullKey, Section);
            StringConcat(FullKey, TEXT("."));
        }
        StringConcat(FullKey, Key);

        // Allocate new TOML item structure
        Item = (LPTOMLITEM)KernelHeapAlloc(sizeof(TOMLITEM));
        if (Item == NULL) continue;
        Item->Next = NULL;

        // Allocate memory for key and value strings
        Item->Key = (LPSTR)KernelHeapAlloc(StringLength(FullKey) + 1);
        Item->Value = (LPSTR)KernelHeapAlloc(StringLength(Value) + 1);

        // Handle allocation failures
        if (Item->Key == NULL || Item->Value == NULL) {
            if (Item->Key) KernelHeapFree(Item->Key);
            if (Item->Value) KernelHeapFree(Item->Value);
            KernelHeapFree(Item);
            continue;
        }

        // Copy the key and value strings
        StringCopy(Item->Key, FullKey);
        StringCopy(Item->Value, Value);

        // Add item to linked list
        if (Toml->First == NULL) {
            Toml->First = Item;  // First item
        } else {
            Last->Next = Item;  // Append to end
        }
        Last = Item;
    }

    DEBUG(TEXT("[TomlParse] Exit"));

    return Toml;
}

/***************************************************************************/

/**
 * @brief Retrieves a value from the TOML structure using a dot-separated path.
 *
 * Searches through the linked list of TOML items to find a key that matches
 * the provided path. The path should use dot notation (e.g., "section.key").
 *
 * @param Toml Pointer to TOML structure to search in
 * @param Path Dot-separated path to the desired value (e.g., "server.port")
 * @return Pointer to value string if found, or NULL if not found or on error
 */
LPCSTR TomlGet(LPTOML Toml, LPCSTR Path) {
    LPTOMLITEM Item = NULL;

    // Validate parameters
    if (Toml == NULL) return NULL;
    if (Path == NULL) return NULL;

    // Search through linked list for matching key
    for (Item = Toml->First; Item; Item = Item->Next) {
        if (STRINGS_EQUAL(Item->Key, Path)) {
            return Item->Value;  // Found matching key
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Frees all memory allocated for a TOML structure and its items.
 *
 * Traverses the linked list of TOML items, freeing the key and value strings
 * for each item, then the item structure itself, and finally the main TOML structure.
 *
 * @param Toml Pointer to TOML structure to free (ignored if NULL)
 */
void TomlFree(LPTOML Toml) {
    LPTOMLITEM Item = NULL;
    LPTOMLITEM Next = NULL;

    DEBUG(TEXT("[TomlFree] Enter"));

    if (Toml == NULL) return;

    // Free all items in the linked list
    for (Item = Toml->First; Item; Item = Next) {
        Next = Item->Next;  // Save next pointer before freeing current item

        // Free the strings
        if (Item->Key) KernelHeapFree(Item->Key);
        if (Item->Value) KernelHeapFree(Item->Value);

        // Free the item structure itself
        KernelHeapFree(Item);
    }

    // Free the main TOML structure
    KernelHeapFree(Toml);

    DEBUG(TEXT("[TomlFree] Exit"));
}

