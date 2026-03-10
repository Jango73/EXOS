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


    Desktop theme common helpers

\************************************************************************/

#include "desktop/Desktop-ThemeCommon.h"

#include "CoreString.h"

/***************************************************************************/

/**
 * @brief Compare start of one string with one prefix.
 * @param Text Input text.
 * @param Prefix Prefix text.
 * @return TRUE when text starts with prefix.
 */
static BOOL DesktopThemeStartsWith(LPCSTR Text, LPCSTR Prefix) {
    UINT Index;

    if (Text == NULL || Prefix == NULL) return FALSE;

    for (Index = 0; Prefix[Index] != STR_NULL; Index++) {
        if (Text[Index] != Prefix[Index]) return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Parse one color literal.
 * @param Value Color text as "#RRGGBB", "#AARRGGBB", "0xRRGGBB", or "0xAARRGGBB".
 * @param Color Receives parsed color.
 * @return TRUE on success.
 */
BOOL DesktopThemeParseColorLiteral(LPCSTR Value, COLOR* Color) {
    STR HexBuffer[16];
    UINT Index;
    UINT Length;

    if (Value == NULL || Color == NULL) return FALSE;

    if (DesktopThemeStartsWith(Value, TEXT("0x")) || DesktopThemeStartsWith(Value, TEXT("0X"))) {
        *Color = StringToU32(Value);
        return TRUE;
    }

    if (Value[0] != '#') return FALSE;

    Length = StringLength(Value);
    if (Length != 7 && Length != 9) return FALSE;

    HexBuffer[0] = '0';
    HexBuffer[1] = 'x';
    for (Index = 1; Index < Length; Index++) {
        HexBuffer[Index + 1] = Value[Index];
    }
    HexBuffer[Length + 1] = STR_NULL;
    *Color = StringToU32(HexBuffer);
    return TRUE;
}
