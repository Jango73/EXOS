
// Key.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Base.h"
#include "Keyboard.h"
#include "VKey.h"

/***************************************************************************/

KEYTRANS ScanCodeToKeyCode_fr [128] =
{
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 0
  { { VK_ESCAPE,        0 }, { VK_ESCAPE,     0 }, { VK_ESCAPE,       0 } }, // 1
  { { VK_NONE,        '&' }, { VK_1,        '1' }, { VK_NONE,         0 } }, // 2
  { { VK_NONE,        '‚' }, { VK_2,        '2' }, { VK_NONE,       '~' } }, // 3
  { { VK_NONE,        '"' }, { VK_3,        '3' }, { VK_NONE,       '#' } }, // 4
  { { VK_NONE,       '\'' }, { VK_4,        '4' }, { VK_NONE,       '{' } }, // 5
  { { VK_NONE,        '(' }, { VK_5,        '5' }, { VK_NONE,       '[' } }, // 6
  { { VK_MINUS,       '-' }, { VK_6,        '6' }, { VK_NONE,       '|' } }, // 7
  { { VK_NONE,        'Š' }, { VK_7,        '7' }, { VK_NONE,       '`' } }, // 8
  { { VK_UNDERSCORE,  '_' }, { VK_8,        '8' }, { VK_BACKSLASH, '\\' } }, // 9
  { { VK_NONE,        '‡' }, { VK_9,        '9' }, { VK_NONE,       '^' } }, // 10
  { { VK_NONE,        '…' }, { VK_0,        '0' }, { VK_AT,         '@' } }, // 11
  { { VK_NONE,        ')' }, { VK_NONE,     'ø' }, { VK_NONE,       ']' } }, // 12
  { { VK_EQUAL,       '=' }, { VK_PLUS,     '+' }, { VK_NONE,       '}' } }, // 13
  { { VK_BACKSPACE,     0 }, { VK_BACKSPACE,  0 }, { VK_BACKSPACE,    0 } }, // 14
  { { VK_TAB,           0 }, { VK_TAB,        0 }, { VK_TAB,          0 } }, // 15
  { { VK_A,           'a' }, { VK_A,        'A' }, { VK_A,          'a' } }, // 16
  { { VK_Z,           'z' }, { VK_Z,        'Z' }, { VK_Z,          'z' } }, // 17
  { { VK_E,           'e' }, { VK_E,        'E' }, { VK_E,          'e' } }, // 18
  { { VK_R,           'r' }, { VK_R,        'R' }, { VK_R,          'r' } }, // 19
  { { VK_T,           't' }, { VK_T,        'T' }, { VK_T,          't' } }, // 20
  { { VK_Y,           'y' }, { VK_Y,        'Y' }, { VK_Y,          'y' } }, // 21
  { { VK_U,           'u' }, { VK_U,        'U' }, { VK_U,          'u' } }, // 22
  { { VK_I,           'i' }, { VK_I,        'I' }, { VK_I,          'i' } }, // 23
  { { VK_O,           'o' }, { VK_O,        'O' }, { VK_O,          'o' } }, // 24
  { { VK_P,           'p' }, { VK_P,        'P' }, { VK_P,          'p' } }, // 25
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 26
  { { VK_DOLLAR,      '$' }, { VK_NONE,     'œ' }, { VK_NONE,       'Ï' } }, // 27
  { { VK_ENTER,        10 }, { VK_ENTER,     10 }, { VK_ENTER,       10 } }, // 28
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 29
  { { VK_Q,           'q' }, { VK_Q,        'Q' }, { VK_Q,          'q' } }, // 30
  { { VK_S,           's' }, { VK_S,        'S' }, { VK_S,          's' } }, // 31
  { { VK_D,           'd' }, { VK_D,        'D' }, { VK_D,          'd' } }, // 32
  { { VK_F,           'f' }, { VK_F,        'F' }, { VK_F,          'f' } }, // 33
  { { VK_G,           'g' }, { VK_G,        'G' }, { VK_G,          'g' } }, // 34
  { { VK_H,           'h' }, { VK_H,        'H' }, { VK_H,          'h' } }, // 35
  { { VK_J,           'j' }, { VK_J,        'J' }, { VK_J,          'j' } }, // 36
  { { VK_K,           'k' }, { VK_K,        'K' }, { VK_K,          'k' } }, // 37
  { { VK_L,           'l' }, { VK_L,        'L' }, { VK_L,          'l' } }, // 38
  { { VK_M,           'm' }, { VK_M,        'M' }, { VK_M,          'm' } }, // 39
  { { VK_NONE,        '—' }, { VK_PERCENT,  '%' }, { VK_NONE,         0 } }, // 40
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 41
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 42
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 43
  { { VK_W,           'w' }, { VK_W,        'W' }, { VK_W,          'w' } }, // 44
  { { VK_X,           'x' }, { VK_X,        'X' }, { VK_X,          'x' } }, // 45
  { { VK_C,           'c' }, { VK_C,        'C' }, { VK_C,          'c' } }, // 46
  { { VK_V,           'v' }, { VK_V,        'V' }, { VK_V,          'v' } }, // 47
  { { VK_B,           'b' }, { VK_B,        'B' }, { VK_B,          'b' } }, // 48
  { { VK_N,           'n' }, { VK_N,        'N' }, { VK_N,          'n' } }, // 49
  { { VK_COMMA,       ',' }, { VK_QUESTION, '?' }, { VK_NONE,         0 } }, // 50
  { { VK_NONE,        ';' }, { VK_DOT,      '.' }, { VK_NONE,         0 } }, // 51
  { { VK_COLON,       ':' }, { VK_SLASH,    '/' }, { VK_NONE,         0 } }, // 52
  { { VK_EXCL,        '!' }, { VK_NONE,     'õ' }, { VK_NONE,         0 } }, // 53
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 54
  { { VK_STAR,        '*' }, { VK_STAR,     '*' }, { VK_STAR,       '*' } }, // 55
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 56
  { { VK_SPACE,       ' ' }, { VK_SPACE,    ' ' }, { VK_SPACE,      ' ' } }, // 57
  { { VK_CAPS,          0 }, { VK_CAPS,       0 }, { VK_CAPS,         0 } }, // 58
  { { VK_F1,            0 }, { VK_F1,         0 }, { VK_F1,           0 } }, // 59
  { { VK_F2,            0 }, { VK_F2,         0 }, { VK_F2,           0 } }, // 60
  { { VK_F3,            0 }, { VK_F3,         0 }, { VK_F3,           0 } }, // 61
  { { VK_F4,            0 }, { VK_F4,         0 }, { VK_F4,           0 } }, // 62
  { { VK_F5,            0 }, { VK_F5,         0 }, { VK_F5,           0 } }, // 63
  { { VK_F6,            0 }, { VK_F6,         0 }, { VK_F6,           0 } }, // 64
  { { VK_F7,            0 }, { VK_F7,         0 }, { VK_F7,           0 } }, // 65
  { { VK_F8,            0 }, { VK_F8,         0 }, { VK_F8,           0 } }, // 66
  { { VK_F9,            0 }, { VK_F9,         0 }, { VK_F9,           0 } }, // 67
  { { VK_F10,           0 }, { VK_F10,        0 }, { VK_F10,          0 } }, // 68
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 69
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 70
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 71
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 72
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 73
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 74
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 75
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 76
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 77
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 78
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 79
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 80
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 81
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 82
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 83
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 84
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 85
  { { VK_NONE,        '<' }, { VK_NONE,     '>' }, { VK_NONE,         0 } }, // 86
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 87
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 88
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 89
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 90
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 91
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 92
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 93
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 94
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 95
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 96
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 97
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 98
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 99
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 100
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 101
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 102
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 103
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 104
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 105
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 106
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 107
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 108
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 109
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 110
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 111
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 112
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 113
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 114
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 115
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 116
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 117
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 118
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 119
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 120
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 121
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 122
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 123
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 124
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 125
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 126
  { { VK_NONE,          0 }, { VK_NONE,       0 }, { VK_NONE,         0 } }, // 127
};

/***************************************************************************/
