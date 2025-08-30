
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


    Key codes

\************************************************************************/

#include "../include/Base.h"
#include "../include/Keyboard.h"
#include "../include/String.h"
#include "../include/VKey.h"

/************************************************************************/

typedef struct tag_KEYNAME {
    U8 VirtualKey;
    LPCSTR String;
} KEYNAME, *LPKEYNAME;

/************************************************************************/

typedef struct tag_KEYBOARDLAYOUT {
    LPCSTR Code;
    LPKEYTRANS Table;
} KEYBOARDLAYOUT, *LPKEYBOARDLAYOUT;

/************************************************************************/

KEYTRANS ScanCodeToKeyCode_frFR[128] = {
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 0
    {{VK_ESCAPE, 0, 0}, {VK_ESCAPE, 0, 0}, {VK_ESCAPE, 0, 0}},           // 1
    {{VK_NONE, '&', 0}, {VK_1, '1', 0}, {VK_NONE, 0, 0}},                // 2
    {{VK_NONE, 'é', 0}, {VK_2, '2', 0}, {VK_NONE, '~', 0}},              // 3
    {{VK_NONE, '"', 0}, {VK_3, '3', 0}, {VK_NONE, '#', 0}},              // 4
    {{VK_NONE, '\'', 0}, {VK_4, '4', 0}, {VK_NONE, '{', 0}},             // 5
    {{VK_NONE, '(', 0}, {VK_5, '5', 0}, {VK_NONE, '[', 0}},              // 6
    {{VK_MINUS, '-', 0}, {VK_6, '6', 0}, {VK_NONE, '|', 0}},             // 7
    {{VK_NONE, 'è', 0}, {VK_7, '7', 0}, {VK_NONE, '`', 0}},              // 8
    {{VK_UNDERSCORE, '_', 0}, {VK_8, '8', 0}, {VK_BACKSLASH, '\\', 0}},  // 9
    {{VK_NONE, '_', 0}, {VK_9, '9', 0}, {VK_NONE, '^', 0}},              // 10
    {{VK_NONE, 'ç', 0}, {VK_0, '0', 0}, {VK_AT, '@', 0}},                // 11
    {{VK_NONE, ')', 0}, {VK_NONE, 'ø', 0}, {VK_NONE, ']', 0}},           // 12
    {{VK_EQUAL, '=', 0}, {VK_PLUS, '+', 0}, {VK_NONE, '}', 0}},          // 13
    {{VK_BACKSPACE, 0, 0}, {VK_BACKSPACE, 0, 0}, {VK_BACKSPACE, 0, 0}},  // 14
    {{VK_TAB, 0, 0}, {VK_TAB, 0, 0}, {VK_TAB, 0, 0}},                    // 15
    {{VK_A, 'a', 0}, {VK_A, 'A', 0}, {VK_A, 'a', 0}},                    // 16
    {{VK_Z, 'z', 0}, {VK_Z, 'Z', 0}, {VK_Z, 'z', 0}},                    // 17
    {{VK_E, 'e', 0}, {VK_E, 'E', 0}, {VK_E, 'e', 0}},                    // 18
    {{VK_R, 'r', 0}, {VK_R, 'R', 0}, {VK_R, 'r', 0}},                    // 19
    {{VK_T, 't', 0}, {VK_T, 'T', 0}, {VK_T, 't', 0}},                    // 20
    {{VK_Y, 'y', 0}, {VK_Y, 'Y', 0}, {VK_Y, 'y', 0}},                    // 21
    {{VK_U, 'u', 0}, {VK_U, 'U', 0}, {VK_U, 'u', 0}},                    // 22
    {{VK_I, 'i', 0}, {VK_I, 'I', 0}, {VK_I, 'i', 0}},                    // 23
    {{VK_O, 'o', 0}, {VK_O, 'O', 0}, {VK_O, 'o', 0}},                    // 24
    {{VK_P, 'p', 0}, {VK_P, 'P', 0}, {VK_P, 'p', 0}},                    // 25
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 26
    {{VK_DOLLAR, '$', 0}, {VK_NONE, 'œ', 0}, {VK_NONE, 'Ï', 0}},         // 27
    {{VK_ENTER, 10, 0}, {VK_ENTER, 10, 0}, {VK_ENTER, 10, 0}},           // 28
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 29
    {{VK_Q, 'q', 0}, {VK_Q, 'Q', 0}, {VK_Q, 'q', 0}},                    // 30
    {{VK_S, 's', 0}, {VK_S, 'S', 0}, {VK_S, 's', 0}},                    // 31
    {{VK_D, 'd', 0}, {VK_D, 'D', 0}, {VK_D, 'd', 0}},                    // 32
    {{VK_F, 'f', 0}, {VK_F, 'F', 0}, {VK_F, 'f', 0}},                    // 33
    {{VK_G, 'g', 0}, {VK_G, 'G', 0}, {VK_G, 'g', 0}},                    // 34
    {{VK_H, 'h', 0}, {VK_H, 'H', 0}, {VK_H, 'h', 0}},                    // 35
    {{VK_J, 'j', 0}, {VK_J, 'J', 0}, {VK_J, 'j', 0}},                    // 36
    {{VK_K, 'k', 0}, {VK_K, 'K', 0}, {VK_K, 'k', 0}},                    // 37
    {{VK_L, 'l', 0}, {VK_L, 'L', 0}, {VK_L, 'l', 0}},                    // 38
    {{VK_M, 'm', 0}, {VK_M, 'M', 0}, {VK_M, 'm', 0}},                    // 39
    {{VK_NONE, 'ù', 0}, {VK_PERCENT, '%', 0}, {VK_NONE, 0, 0}},          // 40
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 41
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 42
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 43
    {{VK_W, 'w', 0}, {VK_W, 'W', 0}, {VK_W, 'w', 0}},                    // 44
    {{VK_X, 'x', 0}, {VK_X, 'X', 0}, {VK_X, 'x', 0}},                    // 45
    {{VK_C, 'c', 0}, {VK_C, 'C', 0}, {VK_C, 'c', 0}},                    // 46
    {{VK_V, 'v', 0}, {VK_V, 'V', 0}, {VK_V, 'v', 0}},                    // 47
    {{VK_B, 'b', 0}, {VK_B, 'B', 0}, {VK_B, 'b', 0}},                    // 48
    {{VK_N, 'n', 0}, {VK_N, 'N', 0}, {VK_N, 'n', 0}},                    // 49
    {{VK_COMMA, ',', 0}, {VK_QUESTION, '?', 0}, {VK_NONE, 0, 0}},        // 50
    {{VK_NONE, ';', 0}, {VK_DOT, '.', 0}, {VK_NONE, 0, 0}},              // 51
    {{VK_COLON, ':', 0}, {VK_SLASH, '/', 0}, {VK_NONE, 0, 0}},           // 52
    {{VK_EXCL, '!', 0}, {VK_NONE, 'õ', 0}, {VK_NONE, 0, 0}},             // 53
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 54
    {{VK_STAR, '*', 0}, {VK_STAR, '*', 0}, {VK_STAR, '*', 0}},           // 55
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 56
    {{VK_SPACE, ' ', 0}, {VK_SPACE, ' ', 0}, {VK_SPACE, ' ', 0}},        // 57
    {{VK_CAPS, 0, 0}, {VK_CAPS, 0, 0}, {VK_CAPS, 0, 0}},                 // 58
    {{VK_F1, 0, 0}, {VK_F1, 0, 0}, {VK_F1, 0, 0}},                       // 59
    {{VK_F2, 0, 0}, {VK_F2, 0, 0}, {VK_F2, 0, 0}},                       // 60
    {{VK_F3, 0, 0}, {VK_F3, 0, 0}, {VK_F3, 0, 0}},                       // 61
    {{VK_F4, 0, 0}, {VK_F4, 0, 0}, {VK_F4, 0, 0}},                       // 62
    {{VK_F5, 0, 0}, {VK_F5, 0, 0}, {VK_F5, 0, 0}},                       // 63
    {{VK_F6, 0, 0}, {VK_F6, 0, 0}, {VK_F6, 0, 0}},                       // 64
    {{VK_F7, 0, 0}, {VK_F7, 0, 0}, {VK_F7, 0, 0}},                       // 65
    {{VK_F8, 0, 0}, {VK_F8, 0, 0}, {VK_F8, 0, 0}},                       // 66
    {{VK_F9, 0, 0}, {VK_F9, 0, 0}, {VK_F9, 0, 0}},                       // 67
    {{VK_F10, 0, 0}, {VK_F10, 0, 0}, {VK_F10, 0, 0}},                    // 68
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 69
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 70
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 71
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 72
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 73
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 74
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 75
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 76
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 77
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 78
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 79
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 80
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 81
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 82
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 83
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 84
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 85
    {{VK_NONE, '<', 0}, {VK_NONE, '>', 0}, {VK_NONE, 0, 0}},             // 86
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 87
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 88
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 89
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 90
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 91
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 92
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 93
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 94
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 95
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 96
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 97
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 98
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 99
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 100
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 101
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 102
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 103
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 104
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 105
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 106
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 107
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 108
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 109
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 110
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 111
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 112
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 113
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 114
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 115
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 116
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 117
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 118
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 119
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 120
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 121
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 122
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 123
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 124
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 125
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 126
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 127
};

/***************************************************************************/

KEYTRANS ScanCodeToKeyCode_enUS[128] = {
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 0
    {{VK_ESCAPE, 0, 0}, {VK_ESCAPE, 0, 0}, {VK_ESCAPE, 0, 0}},           // 1
    {{VK_1, '1', 0}, {VK_EXCL, '!', 0}, {VK_NONE, 0, 0}},                // 2
    {{VK_2, '2', 0}, {VK_AT, '@', 0}, {VK_NONE, 0, 0}},                  // 3
    {{VK_3, '3', 0}, {VK_NONE, '#', 0}, {VK_NONE, 0, 0}},                // 4
    {{VK_4, '4', 0}, {VK_DOLLAR, '$', 0}, {VK_NONE, 0, 0}},              // 5
    {{VK_5, '5', 0}, {VK_PERCENT, '%', 0}, {VK_NONE, 0, 0}},             // 6
    {{VK_6, '6', 0}, {VK_NONE, '^', 0}, {VK_NONE, 0, 0}},                // 7
    {{VK_7, '7', 0}, {VK_NONE, '&', 0}, {VK_NONE, 0, 0}},                // 8
    {{VK_8, '8', 0}, {VK_STAR, '*', 0}, {VK_NONE, 0, 0}},                // 9
    {{VK_9, '9', 0}, {VK_NONE, '(', 0}, {VK_NONE, 0, 0}},                // 10
    {{VK_0, '0', 0}, {VK_NONE, ')', 0}, {VK_NONE, 0, 0}},                // 11
    {{VK_MINUS, '-', 0}, {VK_UNDERSCORE, '_', 0}, {VK_NONE, 0, 0}},      // 12
    {{VK_EQUAL, '=', 0}, {VK_PLUS, '+', 0}, {VK_NONE, 0, 0}},            // 13
    {{VK_BACKSPACE, 0, 0}, {VK_BACKSPACE, 0, 0}, {VK_BACKSPACE, 0, 0}},  // 14
    {{VK_TAB, 0, 0}, {VK_TAB, 0, 0}, {VK_TAB, 0, 0}},                    // 15
    {{VK_Q, 'q', 0}, {VK_Q, 'Q', 0}, {VK_NONE, 0, 0}},                   // 16
    {{VK_W, 'w', 0}, {VK_W, 'W', 0}, {VK_NONE, 0, 0}},                   // 17
    {{VK_E, 'e', 0}, {VK_E, 'E', 0}, {VK_NONE, 0, 0}},                   // 18
    {{VK_R, 'r', 0}, {VK_R, 'R', 0}, {VK_NONE, 0, 0}},                   // 19
    {{VK_T, 't', 0}, {VK_T, 'T', 0}, {VK_NONE, 0, 0}},                   // 20
    {{VK_Y, 'y', 0}, {VK_Y, 'Y', 0}, {VK_NONE, 0, 0}},                   // 21
    {{VK_U, 'u', 0}, {VK_U, 'U', 0}, {VK_NONE, 0, 0}},                   // 22
    {{VK_I, 'i', 0}, {VK_I, 'I', 0}, {VK_NONE, 0, 0}},                   // 23
    {{VK_O, 'o', 0}, {VK_O, 'O', 0}, {VK_NONE, 0, 0}},                   // 24
    {{VK_P, 'p', 0}, {VK_P, 'P', 0}, {VK_NONE, 0, 0}},                   // 25
    {{VK_NONE, '[', 0}, {VK_NONE, '{', 0}, {VK_NONE, 0, 0}},             // 26
    {{VK_NONE, ']', 0}, {VK_NONE, '}', 0}, {VK_NONE, 0, 0}},             // 27
    {{VK_ENTER, 10, 0}, {VK_ENTER, 10, 0}, {VK_ENTER, 10, 0}},           // 28
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 29
    {{VK_A, 'a', 0}, {VK_A, 'A', 0}, {VK_NONE, 0, 0}},                   // 30
    {{VK_S, 's', 0}, {VK_S, 'S', 0}, {VK_NONE, 0, 0}},                   // 31
    {{VK_D, 'd', 0}, {VK_D, 'D', 0}, {VK_NONE, 0, 0}},                   // 32
    {{VK_F, 'f', 0}, {VK_F, 'F', 0}, {VK_NONE, 0, 0}},                   // 33
    {{VK_G, 'g', 0}, {VK_G, 'G', 0}, {VK_NONE, 0, 0}},                   // 34
    {{VK_H, 'h', 0}, {VK_H, 'H', 0}, {VK_NONE, 0, 0}},                   // 35
    {{VK_J, 'j', 0}, {VK_J, 'J', 0}, {VK_NONE, 0, 0}},                   // 36
    {{VK_K, 'k', 0}, {VK_K, 'K', 0}, {VK_NONE, 0, 0}},                   // 37
    {{VK_L, 'l', 0}, {VK_L, 'L', 0}, {VK_NONE, 0, 0}},                   // 38
    {{VK_COLON, ';', 0}, {VK_COLON, ':', 0}, {VK_NONE, 0, 0}},           // 39
    {{VK_NONE, '\'', 0}, {VK_NONE, '"', 0}, {VK_NONE, 0, 0}},            // 40
    {{VK_NONE, '`', 0}, {VK_NONE, '~', 0}, {VK_NONE, 0, 0}},             // 41
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 42
    {{VK_BACKSLASH, '\\', 0}, {VK_BACKSLASH, '|', 0}, {VK_NONE, 0, 0}},  // 43
    {{VK_Z, 'z', 0}, {VK_Z, 'Z', 0}, {VK_NONE, 0, 0}},                   // 44
    {{VK_X, 'x', 0}, {VK_X, 'X', 0}, {VK_NONE, 0, 0}},                   // 45
    {{VK_C, 'c', 0}, {VK_C, 'C', 0}, {VK_NONE, 0, 0}},                   // 46
    {{VK_V, 'v', 0}, {VK_V, 'V', 0}, {VK_NONE, 0, 0}},                   // 47
    {{VK_B, 'b', 0}, {VK_B, 'B', 0}, {VK_NONE, 0, 0}},                   // 48
    {{VK_N, 'n', 0}, {VK_N, 'N', 0}, {VK_NONE, 0, 0}},                   // 49
    {{VK_M, 'm', 0}, {VK_M, 'M', 0}, {VK_NONE, 0, 0}},                   // 50
    {{VK_COMMA, ',', 0}, {VK_COMMA, '<', 0}, {VK_NONE, 0, 0}},           // 51
    {{VK_DOT, '.', 0}, {VK_DOT, '>', 0}, {VK_NONE, 0, 0}},               // 52
    {{VK_SLASH, '/', 0}, {VK_QUESTION, '?', 0}, {VK_NONE, 0, 0}},        // 53
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 54
    {{VK_STAR, '*', 0}, {VK_STAR, '*', 0}, {VK_STAR, '*', 0}},           // 55
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 56
    {{VK_SPACE, ' ', 0}, {VK_SPACE, ' ', 0}, {VK_SPACE, ' ', 0}},        // 57
    {{VK_CAPS, 0, 0}, {VK_CAPS, 0, 0}, {VK_CAPS, 0, 0}},                 // 58
    {{VK_F1, 0, 0}, {VK_F1, 0, 0}, {VK_F1, 0, 0}},                       // 59
    {{VK_F2, 0, 0}, {VK_F2, 0, 0}, {VK_F2, 0, 0}},                       // 60
    {{VK_F3, 0, 0}, {VK_F3, 0, 0}, {VK_F3, 0, 0}},                       // 61
    {{VK_F4, 0, 0}, {VK_F4, 0, 0}, {VK_F4, 0, 0}},                       // 62
    {{VK_F5, 0, 0}, {VK_F5, 0, 0}, {VK_F5, 0, 0}},                       // 63
    {{VK_F6, 0, 0}, {VK_F6, 0, 0}, {VK_F6, 0, 0}},                       // 64
    {{VK_F7, 0, 0}, {VK_F7, 0, 0}, {VK_F7, 0, 0}},                       // 65
    {{VK_F8, 0, 0}, {VK_F8, 0, 0}, {VK_F8, 0, 0}},                       // 66
    {{VK_F9, 0, 0}, {VK_F9, 0, 0}, {VK_F9, 0, 0}},                       // 67
    {{VK_F10, 0, 0}, {VK_F10, 0, 0}, {VK_F10, 0, 0}},                    // 68
    {{VK_NUM, 0, 0}, {VK_NUM, 0, 0}, {VK_NUM, 0, 0}},                    // 69
    {{VK_SCROLL, 0, 0}, {VK_SCROLL, 0, 0}, {VK_SCROLL, 0, 0}},           // 70
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 71
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 72
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 73
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 74
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 75
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 76
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 77
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 78
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 79
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 80
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 81
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 82
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 83
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 84
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 85
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 86
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 87
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 88
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 89
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 90
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 91
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 92
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 93
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 94
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 95
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 96
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 97
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 98
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 99
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 100
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 101
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 102
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 103
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 104
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 105
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 106
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 107
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 108
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 109
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 110
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 111
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 112
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 113
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 114
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 115
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 116
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 117
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 118
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 119
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 120
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 121
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 122
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 123
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 124
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 125
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 126
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                 // 127
};

/************************************************************************/

KEYTRANS ScanCodeToKeyCode_deDE[128] = {
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 0
    {{VK_ESCAPE, 0, 0}, {VK_ESCAPE, 0, 0}, {VK_ESCAPE, 0, 0}},            // 1
    {{VK_1, '1', 0}, {VK_EXCL, '!', 0}, {VK_NONE, 0, 0}},                 // 2
    {{VK_2, '2', 0}, {VK_NONE, '"', 0}, {VK_NONE, 0, 0}},                 // 3
    {{VK_3, '3', 0}, {VK_NONE, '§', 0}, {VK_NONE, 0, 0}},                 // 4
    {{VK_4, '4', 0}, {VK_DOLLAR, '$', 0}, {VK_NONE, 0, 0}},               // 5
    {{VK_5, '5', 0}, {VK_PERCENT, '%', 0}, {VK_NONE, 0, 0}},              // 6
    {{VK_6, '6', 0}, {VK_NONE, '&', 0}, {VK_NONE, 0, 0}},                 // 7
    {{VK_7, '7', 0}, {VK_SLASH, '/', 0}, {VK_NONE, '{', 0}},              // 8
    {{VK_8, '8', 0}, {VK_NONE, '(', 0}, {VK_NONE, '[', 0}},               // 9
    {{VK_9, '9', 0}, {VK_NONE, ')', 0}, {VK_NONE, ']', 0}},               // 10
    {{VK_0, '0', 0}, {VK_EQUAL, '=', 0}, {VK_NONE, '}', 0}},              // 11
    {{VK_NONE, 'ß', 0}, {VK_QUESTION, '?', 0}, {VK_BACKSLASH, '\\', 0}},  // 12
    {{VK_NONE, '´', 0}, {VK_NONE, '`', 0}, {VK_NONE, 0, 0}},              // 13
    {{VK_BACKSPACE, 0, 0}, {VK_BACKSPACE, 0, 0}, {VK_BACKSPACE, 0, 0}},   // 14
    {{VK_TAB, 0, 0}, {VK_TAB, 0, 0}, {VK_TAB, 0, 0}},                     // 15
    {{VK_Q, 'q', 0}, {VK_Q, 'Q', 0}, {VK_AT, '@', 0}},                    // 16
    {{VK_W, 'w', 0}, {VK_W, 'W', 0}, {VK_NONE, 0, 0}},                    // 17
    {{VK_E, 'e', 0}, {VK_E, 'E', 0}, {VK_NONE, 'e', 0}},                  // 18
    {{VK_R, 'r', 0}, {VK_R, 'R', 0}, {VK_NONE, 0, 0}},                    // 19
    {{VK_T, 't', 0}, {VK_T, 'T', 0}, {VK_NONE, 0, 0}},                    // 20
    {{VK_Z, 'z', 0}, {VK_Z, 'Z', 0}, {VK_NONE, 0, 0}},                    // 21
    {{VK_U, 'u', 0}, {VK_U, 'U', 0}, {VK_NONE, 0, 0}},                    // 22
    {{VK_I, 'i', 0}, {VK_I, 'I', 0}, {VK_NONE, 0, 0}},                    // 23
    {{VK_O, 'o', 0}, {VK_O, 'O', 0}, {VK_NONE, 0, 0}},                    // 24
    {{VK_P, 'p', 0}, {VK_P, 'P', 0}, {VK_NONE, 0, 0}},                    // 25
    {{VK_NONE, 'ü', 0}, {VK_NONE, 'Ü', 0}, {VK_NONE, 0, 0}},              // 26
    {{VK_PLUS, '+', 0}, {VK_STAR, '*', 0}, {VK_NONE, '~', 0}},            // 27
    {{VK_ENTER, 10, 0}, {VK_ENTER, 10, 0}, {VK_ENTER, 10, 0}},            // 28
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 29
    {{VK_A, 'a', 0}, {VK_A, 'A', 0}, {VK_NONE, 0, 0}},                    // 30
    {{VK_S, 's', 0}, {VK_S, 'S', 0}, {VK_NONE, 0, 0}},                    // 31
    {{VK_D, 'd', 0}, {VK_D, 'D', 0}, {VK_NONE, 0, 0}},                    // 32
    {{VK_F, 'f', 0}, {VK_F, 'F', 0}, {VK_NONE, 0, 0}},                    // 33
    {{VK_G, 'g', 0}, {VK_G, 'G', 0}, {VK_NONE, 0, 0}},                    // 34
    {{VK_H, 'h', 0}, {VK_H, 'H', 0}, {VK_NONE, 0, 0}},                    // 35
    {{VK_J, 'j', 0}, {VK_J, 'J', 0}, {VK_NONE, 0, 0}},                    // 36
    {{VK_K, 'k', 0}, {VK_K, 'K', 0}, {VK_NONE, 0, 0}},                    // 37
    {{VK_L, 'l', 0}, {VK_L, 'L', 0}, {VK_NONE, 0, 0}},                    // 38
    {{VK_NONE, 'ö', 0}, {VK_NONE, 'Ö', 0}, {VK_NONE, 0, 0}},              // 39
    {{VK_NONE, 'ä', 0}, {VK_NONE, 'Ä', 0}, {VK_NONE, 0, 0}},              // 40
    {{VK_NONE, '^', 0}, {VK_NONE, '°', 0}, {VK_NONE, 0, 0}},              // 41
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 42
    {{VK_NONE, '#', 0}, {VK_NONE, '\'', 0}, {VK_NONE, '|', 0}},           // 43
    {{VK_Y, 'y', 0}, {VK_Y, 'Y', 0}, {VK_NONE, 0, 0}},                    // 44
    {{VK_X, 'x', 0}, {VK_X, 'X', 0}, {VK_NONE, 0, 0}},                    // 45
    {{VK_C, 'c', 0}, {VK_C, 'C', 0}, {VK_NONE, 0, 0}},                    // 46
    {{VK_V, 'v', 0}, {VK_V, 'V', 0}, {VK_NONE, 0, 0}},                    // 47
    {{VK_B, 'b', 0}, {VK_B, 'B', 0}, {VK_NONE, 0, 0}},                    // 48
    {{VK_N, 'n', 0}, {VK_N, 'N', 0}, {VK_NONE, 0, 0}},                    // 49
    {{VK_M, 'm', 0}, {VK_M, 'M', 0}, {VK_NONE, 'µ', 0}},                  // 50
    {{VK_COMMA, ',', 0}, {VK_COMMA, ';', 0}, {VK_NONE, 0, 0}},            // 51
    {{VK_DOT, '.', 0}, {VK_DOT, ':', 0}, {VK_NONE, 0, 0}},                // 52
    {{VK_MINUS, '-', 0}, {VK_UNDERSCORE, '_', 0}, {VK_NONE, 0, 0}},       // 53
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 54
    {{VK_STAR, '*', 0}, {VK_STAR, '*', 0}, {VK_STAR, '*', 0}},            // 55
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 56
    {{VK_SPACE, ' ', 0}, {VK_SPACE, ' ', 0}, {VK_SPACE, ' ', 0}},         // 57
    {{VK_CAPS, 0, 0}, {VK_CAPS, 0, 0}, {VK_CAPS, 0, 0}},                  // 58
    {{VK_F1, 0, 0}, {VK_F1, 0, 0}, {VK_F1, 0, 0}},                        // 59
    {{VK_F2, 0, 0}, {VK_F2, 0, 0}, {VK_F2, 0, 0}},                        // 60
    {{VK_F3, 0, 0}, {VK_F3, 0, 0}, {VK_F3, 0, 0}},                        // 61
    {{VK_F4, 0, 0}, {VK_F4, 0, 0}, {VK_F4, 0, 0}},                        // 62
    {{VK_F5, 0, 0}, {VK_F5, 0, 0}, {VK_F5, 0, 0}},                        // 63
    {{VK_F6, 0, 0}, {VK_F6, 0, 0}, {VK_F6, 0, 0}},                        // 64
    {{VK_F7, 0, 0}, {VK_F7, 0, 0}, {VK_F7, 0, 0}},                        // 65
    {{VK_F8, 0, 0}, {VK_F8, 0, 0}, {VK_F8, 0, 0}},                        // 66
    {{VK_F9, 0, 0}, {VK_F9, 0, 0}, {VK_F9, 0, 0}},                        // 67
    {{VK_F10, 0, 0}, {VK_F10, 0, 0}, {VK_F10, 0, 0}},                     // 68
    {{VK_NUM, 0, 0}, {VK_NUM, 0, 0}, {VK_NUM, 0, 0}},                     // 69
    {{VK_SCROLL, 0, 0}, {VK_SCROLL, 0, 0}, {VK_SCROLL, 0, 0}},            // 70
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 71
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 72
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 73
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 74
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 75
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 76
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 77
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 78
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 79
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 80
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 81
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 82
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 83
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 84
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 85
    {{VK_NONE, '<', 0}, {VK_NONE, '>', 0}, {VK_NONE, '|', 0}},            // 86
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 87
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 88
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 89
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 90
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 91
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 92
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 93
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 94
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 95
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 96
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 97
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 98
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 99
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 100
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 101
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 102
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 103
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 104
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 105
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 106
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 107
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 108
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 109
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 110
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 111
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 112
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 113
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 114
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 115
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 116
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 117
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 118
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 119
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 120
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 121
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 122
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 123
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 124
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 125
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 126
    {{VK_NONE, 0, 0}, {VK_NONE, 0, 0}, {VK_NONE, 0, 0}},                  // 127
};

/***************************************************************************/

KEYBOARDLAYOUT ScanCodeToKeyCode[] = {
    {TEXT("fr-FR"), ScanCodeToKeyCode_frFR},
    {TEXT("en-US"), ScanCodeToKeyCode_enUS},
    {TEXT("de-DE"), ScanCodeToKeyCode_deDE}};

/***************************************************************************/

LPKEYTRANS GetScanCodeToKeyCode(LPCSTR Code) {
    U32 Index;

    for (Index = 0; Index < sizeof(ScanCodeToKeyCode) / sizeof(KEYBOARDLAYOUT); Index++) {
        if (StringCompare(ScanCodeToKeyCode[Index].Code, Code) == 0) {
            return ScanCodeToKeyCode[Index].Table;
        }
    }

    return NULL;
}

/***************************************************************************/

static KEYNAME KeyNames[] = {{VK_NONE, TEXT("NONE")},   {VK_F1, TEXT("F1")},        {VK_F2, TEXT("F2")},
                             {VK_F3, TEXT("F3")},       {VK_F4, TEXT("F4")},        {VK_F5, TEXT("F5")},
                             {VK_F6, TEXT("F6")},       {VK_F7, TEXT("F7")},        {VK_F8, TEXT("F8")},
                             {VK_F9, TEXT("F9")},       {VK_F10, TEXT("F10")},      {VK_F11, TEXT("F11")},
                             {VK_F12, TEXT("F12")},     {VK_0, TEXT("0")},          {VK_1, TEXT("1")},
                             {VK_2, TEXT("2")},         {VK_3, TEXT("3")},          {VK_4, TEXT("4")},
                             {VK_5, TEXT("5")},         {VK_6, TEXT("6")},          {VK_7, TEXT("7")},
                             {VK_8, TEXT("8")},         {VK_9, TEXT("9")},          {VK_A, TEXT("A")},
                             {VK_B, TEXT("B")},         {VK_C, TEXT("C")},          {VK_D, TEXT("D")},
                             {VK_E, TEXT("E")},         {VK_F, TEXT("F")},          {VK_G, TEXT("G")},
                             {VK_H, TEXT("H")},         {VK_I, TEXT("I")},          {VK_J, TEXT("J")},
                             {VK_K, TEXT("K")},         {VK_L, TEXT("L")},          {VK_M, TEXT("M")},
                             {VK_N, TEXT("N")},         {VK_O, TEXT("O")},          {VK_P, TEXT("P")},
                             {VK_Q, TEXT("Q")},         {VK_R, TEXT("R")},          {VK_S, TEXT("S")},
                             {VK_T, TEXT("T")},         {VK_U, TEXT("U")},          {VK_V, TEXT("V")},
                             {VK_W, TEXT("W")},         {VK_X, TEXT("X")},          {VK_Y, TEXT("Y")},
                             {VK_Z, TEXT("Z")},         {VK_DOT, TEXT(".")},        {VK_COLON, TEXT(":")},
                             {VK_COMMA, TEXT(",")},     {VK_UNDERSCORE, TEXT("_")}, {VK_STAR, TEXT("*")},
                             {VK_PERCENT, TEXT("%")},   {VK_EQUAL, TEXT("=")},      {VK_PLUS, TEXT("+")},
                             {VK_MINUS, TEXT("-")},     {VK_SLASH, TEXT("/")},      {VK_BACKSLASH, TEXT("\\")},
                             {VK_QUESTION, TEXT("?")},  {VK_EXCL, TEXT("!")},       {VK_DOLLAR, TEXT("$")},
                             {VK_AT, TEXT("@")},        {VK_SPACE, TEXT("SPACE")},  {VK_ENTER, TEXT("ENTER")},
                             {VK_ESCAPE, TEXT("ESC")},  {VK_SHIFT, TEXT("SHFT")},   {VK_LSHIFT, TEXT("LSHF")},
                             {VK_RSHIFT, TEXT("RSHF")}, {VK_CONTROL, TEXT("CTRL")}, {VK_LCTRL, TEXT("LCTL")},
                             {VK_RCTRL, TEXT("RCTL")},  {VK_ALT, TEXT("ALT")},      {VK_LALT, TEXT("LALT")},
                             {VK_RALT, TEXT("RALT")},   {VK_TAB, TEXT("TAB")},      {VK_BACKSPACE, TEXT("BKSP")},
                             {VK_INSERT, TEXT("INS")},  {VK_DELETE, TEXT("DEL")},   {VK_HOME, TEXT("HOME")},
                             {VK_END, TEXT("END")},     {VK_PAGEUP, TEXT("PGUP")},  {VK_PAGEDOWN, TEXT("PGDN")},
                             {VK_UP, TEXT("UP")},       {VK_DOWN, TEXT("DOWN")},    {VK_LEFT, TEXT("LEFT")},
                             {VK_RIGHT, TEXT("RIGHT")}, {VK_NUM, TEXT("NUM")},      {VK_CAPS, TEXT("CAPS")},
                             {VK_SCROLL, TEXT("SCRL")}, {VK_PAUSE, TEXT("PAUS")}};

/***************************************************************************/

LPCSTR GetKeyName(U8 VirtualKey) {
    U32 Index;

    for (Index = 0; Index < sizeof(KeyNames) / sizeof(KEYNAME); Index++) {
        if (KeyNames[Index].VirtualKey == VirtualKey) {
            return KeyNames[Index].String;
        }
    }

    return "";
}
