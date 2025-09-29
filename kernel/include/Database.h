
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


    Database

\************************************************************************/

#ifndef DATABASE_H
#define DATABASE_H

#include "Base.h"

#define DB_FILE_MAGIC 0x44424731u /* "DBG1" */
#define DB_FILE_VERSION 1u

typedef struct {
    U32 Magic;
    U32 Version;
    U32 RecordSize;
    U32 Count;
    U32 Capacity;
} DATABASE_FILE_HEADER;

typedef struct {
    I32 Key;   /* id, -1 if empty */
    U32 Index; /* position in records array */
} DATABASE_INDEX_ENTRY;

typedef struct {
    LPVOID Records; /* array of records */
    U32 RecordSize;
    U32 IdOffset; /* offset of I32 id field */
    U32 Capacity;
    U32 Count;

    DATABASE_INDEX_ENTRY *Index; /* hashmap */
    U32 IndexSize;
} DATABASE;

/* lifecycle */
DATABASE *DatabaseCreate(U32 RecordSize, U32 IdOffset, U32 Capacity);
void DatabaseFree(DATABASE *Database);

/* persistence */
I32 DatabaseSave(DATABASE *Database, LPCSTR Filename);
I32 DatabaseLoad(DATABASE *Database, LPCSTR Filename);

/* CRUD */
I32 DatabaseAdd(DATABASE *Database, LPCVOID Record);
LPVOID DatabaseFind(DATABASE *Database, I32 Id);
I32 DatabaseDelete(DATABASE *Database, I32 Id);

#endif  // DATABASE_H
