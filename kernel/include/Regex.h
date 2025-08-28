
/************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\************************************************************************/

#ifndef REGEX_H_INCLUDED
#define REGEX_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

// Tunable limits
#define REGEX_MAX_PATTERN     1024u   /* max pattern bytes copied */
#define REGEX_MAX_TOKENS       512u   /* max tokens compiled     */

// Token types
typedef U32 TOKEN_TYPE;
#define TT_END       0u  /* end marker */
#define TT_CHAR      1u  /* literal char */
#define TT_DOT       2u  /* '.' */
#define TT_CLASS     3u  /* character class */
#define TT_BOL       4u  /* ^ */
#define TT_EOL       5u  /* $ */
#define TT_STAR      6u  /* *  (applies to previous atom) */
#define TT_PLUS      7u  /* +  (applies to previous atom) */
#define TT_QMARK     8u  /* ?  (applies to previous atom) */

// Character class (256-bit)
typedef struct tag_CHAR_CLASS {
    U8 Bits[32]; /* 256 bits */
    U8 Neg;      /* 1 if negated */
} CHAR_CLASS;

// Token
typedef struct tag_TOKEN {
    TOKEN_TYPE Type;
    U8 Ch;            /* for TT_CHAR */
    CHAR_CLASS Class; /* for TT_CLASS */
} TOKEN;

// Compiled regex
typedef struct tag_REGEX {
    // Copy of pattern for debugging/consistency
    U8 Pattern[REGEX_MAX_PATTERN];

    // Token stream
    TOKEN Tokens[REGEX_MAX_TOKENS];
    U32 TokenCount;

    // Flags derived from anchors
    U8 AnchorBOL;
    U8 AnchorEOL;

    // Compile status
    U8 CompileOk;
} REGEX;

/************************************************************************/

BOOL RegexCompile(CONST LPCSTR Pattern, REGEX* OutRegex);
BOOL RegexMatch(CONST REGEX* Rx, CONST LPCSTR Text);                         // match anywhere
BOOL RegexSearch(CONST REGEX* Rx, CONST LPCSTR Text, U32* OutStart, U32* OutEnd); // first match span
void RegexFree(REGEX* Rx); // no-op in this V1

#endif  // REGEX_H_INCLUDED
