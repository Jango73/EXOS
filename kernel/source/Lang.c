
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


    Lang

\************************************************************************/

#include "../include/Lang.h"

#include "../include/Kernel.h"
#include "../include/Keyboard.h"
#include "../include/Log.h"
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
