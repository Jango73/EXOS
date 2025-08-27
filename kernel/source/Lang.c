/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Lang.h"
#include "../include/Kernel.h"
#include "../include/Keyboard.h"
#include "../include/String.h"

/***************************************************************************/

void SelectLanguage(LPCSTR Code) {
    if (Code) {
        StringCopy(Kernel.LanguageCode, Code);
    }
}

/***************************************************************************/

void SelectKeyboard(LPCSTR Code) {
    if (Code) {
        StringCopy(Kernel.KeyboardCode, Code);
        UseKeyboardLayout(Code);
    }
}

/***************************************************************************/
