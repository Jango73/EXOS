/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef TOML_H_INCLUDED
#define TOML_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

typedef struct tag_TOMLITEM {
    LPSTR Key;
    LPSTR Value;
    struct tag_TOMLITEM* Next;
} TOMLITEM, *LPTOMLITEM;

typedef struct tag_TOML {
    LPTOMLITEM First;
} TOML, *LPTOML;

/***************************************************************************/

LPTOML TomlParse(LPCSTR Source);
LPCSTR TomlGet(LPTOML Toml, LPCSTR Path);
void TomlFree(LPTOML Toml);

/***************************************************************************/

#endif
