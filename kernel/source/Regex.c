
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

    Minimal Regex Engine

    Features:
        - Literals, '.'
        - Char classes: [abc], ranges [a-z], negation [^...]
        - Suffix quantifiers: *, +, ?
        - Anchors: ^ (BOL), $ (EOL)
        - Escapes: \\, \[, \], \., \*, \+, \?, \^, \$, \-
    Non-Features (for V1):
        - No grouping '()'
        - No alternation '|'
        - No {m,n}
        - ASCII only

    API:
        typedef struct REGEX REGEX;

        BOOL RegexCompile(CONST LPCSTR Pattern, REGEX* OutRegex);
        BOOL RegexMatch(CONST REGEX* Rx, CONST LPCSTR Text);                         // match anywhere
        BOOL RegexSearch(CONST REGEX* Rx, CONST LPCSTR Text, U32* OutStart, U32* OutEnd); // first match span
        void RegexFree(REGEX* Rx); // no-op in this V1

    Notes:
        - Deterministic backtracking on a token stream (no catastrophic explosion for supported ops).
        - No heap usage; all buffers bounded. Tune constants below as needed.
        - All identifiers follow your naming conventions (PascalCase, SCREAMING_SNAKE_CASE).

\************************************************************************/

#include "../include/Regex.h"

#include "../include/Base.h"
#include "../include/String.h"

/************************************************************************/
// Internal helpers

static void MemZero(void* Ptr, U32 Size) {
    U8* P = (U8*)Ptr;
    while (Size--) *P++ = 0;
}

static void ClassClear(CHAR_CLASS* C) {
    MemZero(C->Bits, (U32)sizeof(C->Bits));
    C->Neg = 0;
}

static void ClassSet(CHAR_CLASS* C, U32 Ch) {
    C->Bits[Ch >> 3] |= (U8)(1u << (Ch & 7));
}

static BOOL ClassHas(CONST CHAR_CLASS* C, U32 Ch) {
    U8 In = (U8)((C->Bits[Ch >> 3] >> (Ch & 7)) & 1u);
    return C->Neg ? (In == 0) : (In != 0);
}

static void ClassAddRange(CHAR_CLASS* C, U32 A, U32 B) {
    if (A > B) { U32 T = A; A = B; B = T; }
    for (U32 X = A; X <= B; ++X) ClassSet(C, X);
}

/************************************************************************/

/* Read escaped char: advances *P, writes out char (ASCII) */
static BOOL ReadEscapedChar(LPCSTR* P, U8* OutCh) {
    LPCSTR S = *P;
    if (*S != '\\') return FALSE;
    ++S;
    STR C = *S;
    if (C == STR_NULL) return FALSE;
    switch (C) {
        case 'n': *OutCh = (U8)'\n'; break;
        case 'r': *OutCh = (U8)'\r'; break;
        case 't': *OutCh = (U8)'\t'; break;
        case '\\': case '[': case ']': case '.':
        case '*': case '+': case '?': case '^': case '$': case '-':
            *OutCh = (U8)C; break;
        default:
            *OutCh = (U8)C; /* treat unknown escapes as literal char */
            break;
    }
    *P = S + 1;
    return TRUE;
}

/************************************************************************/

/* Parse character class starting at '['; advances *P past ']' */
static BOOL ParseClass(LPCSTR* P, CHAR_CLASS* Out) {
    LPCSTR S = *P;
    if (*S != '[') return FALSE;

    ++S;
    ClassClear(Out);

    /* Negation */
    if (*S == '^') { Out->Neg = 1; ++S; }

    BOOL First = TRUE;
    U8 Prev = 0;
    while (*S && *S != ']') {
        U8 A = 0;
        if (*S == '\\') {
            if (!ReadEscapedChar(&S, &A)) return FALSE;
        } else {
            A = (U8)*S++;
        }

        if (!First && *S == '-' && S[1] != ']' && S[1] != STR_NULL) {
            /* range Prev-A..B */
            ++S; /* skip '-' */
            U8 B = 0;
            if (*S == '\\') {
                if (!ReadEscapedChar(&S, &B)) return FALSE;
            } else {
                B = (U8)*S++;
            }
            ClassAddRange(Out, (U32)Prev, (U32)B);
            Prev = 0;
            First = TRUE; /* reset context */
        } else {
            ClassSet(Out, (U32)A);
            Prev = A;
            First = FALSE;
        }
    }

    if (*S != ']') return FALSE; /* missing ']' */
    *P = S + 1;
    return TRUE;
}

/************************************************************************/

// Append a token to Out->Tokens
static BOOL EmitToken(REGEX* Out, TOKEN_TYPE Type, U8 Ch, CONST CHAR_CLASS* Cls) {
    if (Out->TokenCount >= REGEX_MAX_TOKENS) return FALSE;
    TOKEN* T = &Out->Tokens[Out->TokenCount++];
    T->Type = Type;
    T->Ch = Ch;
    if (Cls) {
        T->Class = *Cls;
    } else {
        MemZero(&T->Class, (U32)sizeof(T->Class));
    }
    return TRUE;
}

/************************************************************************/

// Compile pattern -> tokens
BOOL RegexCompile(CONST LPCSTR Pattern, REGEX* OutRegex) {
    if (OutRegex == NULL || Pattern == NULL) return FALSE;

    MemZero(OutRegex, (U32)sizeof(*OutRegex));

    /* Copy pattern (bounded) */
    U32 L = StringLength(Pattern);
    if (L >= REGEX_MAX_PATTERN) L = REGEX_MAX_PATTERN - 1;
    for (U32 i = 0; i < L; ++i) OutRegex->Pattern[i] = (U8)Pattern[i];
    OutRegex->Pattern[L] = 0;

    LPCSTR P = Pattern;

    // Optional leading ^
    if (*P == '^') {
        if (!EmitToken(OutRegex, TT_BOL, 0, NULL)) return FALSE;
        OutRegex->AnchorBOL = 1;
        ++P;
    }

    while (*P != STR_NULL) {
        STR C = *P;

        if (C == '$' && P[1] == STR_NULL) {
            if (!EmitToken(OutRegex, TT_EOL, 0, NULL)) return FALSE;
            OutRegex->AnchorEOL = 1;
            ++P;
            break;
        }
        else if (C == '.') {
            if (!EmitToken(OutRegex, TT_DOT, 0, NULL)) return FALSE;
            ++P;
        }
        else if (C == '[') {
            CHAR_CLASS CC;
            if (!ParseClass(&P, &CC)) return FALSE;
            if (!EmitToken(OutRegex, TT_CLASS, 0, &CC)) return FALSE;
        }
        else if (C == '*' || C == '+' || C == '?') {
            /* quantifier applies to previous atom */
            TOKEN_TYPE Q = (C == '*') ? TT_STAR : (C == '+') ? TT_PLUS : TT_QMARK;
            /* must have something before */
            if (OutRegex->TokenCount == 0) return FALSE;
            /* previous must be an atom (CHAR/DOT/CLASS or EOL/BOL is invalid target) */
            TOKEN_TYPE Prev = OutRegex->Tokens[OutRegex->TokenCount - 1].Type;
            if (!(Prev == TT_CHAR || Prev == TT_DOT || Prev == TT_CLASS)) return FALSE;
            if (!EmitToken(OutRegex, Q, 0, NULL)) return FALSE;
            ++P;
        }
        else if (C == '\\') {
            U8 Lit = 0;
            if (!ReadEscapedChar(&P, &Lit)) return FALSE;
            if (!EmitToken(OutRegex, TT_CHAR, Lit, NULL)) return FALSE;
        }
        else if (C == '^') {
            /* '^' in middle treated as literal unless you want multiline */
            if (!EmitToken(OutRegex, TT_CHAR, (U8)'^', NULL)) return FALSE;
            ++P;
        }
        else if (C == '$') {
            /* '$' in middle treated as literal (simple policy) */
            if (!EmitToken(OutRegex, TT_CHAR, (U8)'$', NULL)) return FALSE;
            ++P;
        }
        else {
            if (!EmitToken(OutRegex, TT_CHAR, (U8)C, NULL)) return FALSE;
            ++P;
        }
    }

    /* End marker */
    if (!EmitToken(OutRegex, TT_END, 0, NULL)) return FALSE;

    OutRegex->CompileOk = 1;
    return TRUE;
}

/************************************************************************/

// Matching engine (tokens)

// Match a single atom (CHAR/DOT/CLASS) against one input byte.
// Returns 1 if it consumes one char (and writes *NextText), 0 otherwise.
static BOOL MatchOne(CONST TOKEN* Atom, CONST U8* Text, CONST U8** NextText) {
    if (*Text == 0) return FALSE;

    switch (Atom->Type) {
        case TT_CHAR:
            if ((U8)*Text != Atom->Ch) return FALSE;
            *NextText = Text + 1;
            return TRUE;
        case TT_DOT:
            *NextText = Text + 1;
            return TRUE;
        case TT_CLASS:
            if (!ClassHas(&Atom->Class, (U32)(U8)*Text)) return FALSE;
            *NextText = Text + 1;
            return TRUE;
        default:
            return FALSE;
    }
}

/************************************************************************/

static BOOL MatchHere(CONST TOKEN* Toks, U32 PosTok, CONST U8* Text);

/************************************************************************/

static BOOL MatchRepeatGreedy(CONST TOKEN* Toks, U32 AtomPos, TOKEN_TYPE Quant, U32 AfterPos, CONST U8* Text) {
    /* Count how many chars match the atom greedily */
    CONST TOKEN* Atom = &Toks[AtomPos];
    CONST U8* P = Text;
    CONST U8* Q = Text;

    U32 Count = 0;
    CONST U8* Next = P;
    while (MatchOne(Atom, P, &Next)) {
        P = Next;
        ++Count;
    }

    /* For '+' we need at least 1 char; for '*' zero is fine */
    U32 Min = (Quant == TT_PLUS) ? 1u : 0u;

    /* Try backtracking from max to min */
    for (U32 Take = Count; Take + 1u > 0u; --Take) {
        if (Take < Min) break;
        Q = Text;
        for (U32 i = 0; i < Take; ++i) {
            CONST U8* Tmp = Q;
            if (!MatchOne(Atom, Q, &Tmp)) return FALSE; /* should not happen */
            Q = Tmp;
        }
        if (MatchHere(Toks, AfterPos, Q)) return TRUE;
        if (Take == 0) break;
    }
    return FALSE;
}

/************************************************************************/

static BOOL MatchOptional(CONST TOKEN* Toks, U32 AtomPos, U32 AfterPos, CONST U8* Text) {
    CONST U8* Next = Text;
    if (MatchOne(&Toks[AtomPos], Text, &Next)) {
        if (MatchHere(Toks, AfterPos, Next)) return TRUE;
    }
    return MatchHere(Toks, AfterPos, Text);
}

/************************************************************************/

static BOOL MatchHere(CONST TOKEN* Toks, U32 PosTok, CONST U8* Text) {
    for (;;) {
        CONST TOKEN* T = &Toks[PosTok];

        switch (T->Type) {
            case TT_END:
                return TRUE;

            case TT_EOL:
                /* EOL only matches if we're at end of string */
                return (*Text == 0);

            case TT_CHAR:
            case TT_DOT:
            case TT_CLASS: {
                /* Lookahead for quantifier */
                TOKEN_TYPE NextType = Toks[PosTok + 1].Type;
                if (NextType == TT_STAR || NextType == TT_PLUS) {
                    return MatchRepeatGreedy(Toks, PosTok, NextType, PosTok + 2, Text);
                } else if (NextType == TT_QMARK) {
                    return MatchOptional(Toks, PosTok, PosTok + 2, Text);
                } else {
                    CONST U8* NextText = Text;
                    if (!MatchOne(T, Text, &NextText)) return FALSE;
                    Text = NextText;
                    ++PosTok;
                    continue;
                }
            }

            case TT_BOL:
                /* Must be at start (handled by caller if not anchored) */
                if (Text != (CONST U8*)Text) { /* unreachable, kept for symmetry */
                    return FALSE;
                }
                ++PosTok;
                continue;

            default:
                return FALSE;
        }
    }
}

/************************************************************************/

BOOL RegexMatch(CONST REGEX* Rx, CONST LPCSTR Text) {
    if (Rx == NULL || Rx->CompileOk == 0 || Text == NULL) return FALSE;

    CONST TOKEN* Toks = Rx->Tokens;

    if (Rx->AnchorBOL) {
        /* anchored at start */
        return MatchHere(Toks, 0, (CONST U8*)Text);
    } else {
        /* try every position */
        CONST U8* S = (CONST U8*)Text;
        for (; *S; ++S) {
            if (MatchHere(Toks, 0, S)) return TRUE;
        }
        /* allow matching empty at end if pattern can match empty and $ present */
        return MatchHere(Toks, 0, S);
    }
}

/************************************************************************/

/* First match span [start,end) */
BOOL RegexSearch(CONST REGEX* Rx, CONST LPCSTR Text, U32* OutStart, U32* OutEnd) {
    if (Rx == NULL || Rx->CompileOk == 0 || Text == NULL) return FALSE;

    CONST TOKEN* Toks = Rx->Tokens;

    if (Rx->AnchorBOL) {
        CONST U8* S = (CONST U8*)Text;
        CONST U8* T = S;
        if (!MatchHere(Toks, 0, S)) return FALSE;

        /* naive way to find end: advance until next char fails */
        while (*T && MatchHere(Toks, 0, T)) ++T;
        if (OutStart) *OutStart = 0;
        if (OutEnd)   *OutEnd   = (U32)(T - (CONST U8*)Text);
        return TRUE;
    } else {
        CONST U8* Base = (CONST U8*)Text;
        for (CONST U8* S = Base; ; ++S) {
            if (MatchHere(Toks, 0, S)) {
                /* find shortest end >= S */
                CONST U8* T = S;
                while (*T && MatchHere(Toks, 0, T)) ++T;
                if (OutStart) *OutStart = (U32)(S - Base);
                if (OutEnd)   *OutEnd   = (U32)(T - Base);
                return TRUE;
            }
            if (*S == 0) break;
        }
        return FALSE;
    }
}

/************************************************************************/

void RegexFree(REGEX* Rx) {
    /* No dynamic allocation in V1 */
    UNUSED(Rx);
}
