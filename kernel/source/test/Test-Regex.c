
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../../include/Base.h"
#include "../../include/Console.h"
#include "../../include/Regex.h"
#include "../../include/String.h"

static void TestRegex(LPCSTR Pattern, LPCSTR Text) {
    REGEX Rx;
    BOOL Ok = RegexCompile(Pattern, &Rx);
    if (!Ok) {
        ConsolePrint(TEXT("Regex compile failed: %s\n"), Pattern);
        return;
    }

    BOOL Match = RegexMatch(&Rx, Text);
    U32 Start = 0, End = 0;
    BOOL Search = RegexSearch(&Rx, Text, &Start, &End);

    ConsolePrint(TEXT("Pattern: \"%s\"\n"), Pattern);
    ConsolePrint(TEXT("Text   : \"%s\"\n"), Text);
    ConsolePrint(TEXT("Match? : %s\n"), Match ? TEXT("YES") : TEXT("NO"));
    if (Search) {
        ConsolePrint(TEXT("Search : YES (span %u..%u)\n"), Start, End);
    } else {
        ConsolePrint(TEXT("Search : NO\n"));
    }
    ConsolePrint(TEXT("\n"));
}

void RegexSelfTest(void) {
    ConsolePrint(TEXT("=== REGEX SELF TEST ===\n"));

    TestRegex(TEXT("^[A-Za-z_][A-Za-z0-9_]*$"), TEXT("Hello_123"));
    TestRegex(TEXT("^[A-Za-z_][A-Za-z0-9_]*$"), TEXT("123Oops"));
    TestRegex(TEXT("^h.llo$"), TEXT("hello"));
    TestRegex(TEXT("^h.llo$"), TEXT("hallo"));
    TestRegex(TEXT("^h.llo$"), TEXT("hxllo"));
    TestRegex(TEXT("ab*c"), TEXT("ac"));
    TestRegex(TEXT("ab*c"), TEXT("abc"));
    TestRegex(TEXT("ab*c"), TEXT("abbbc"));
    TestRegex(TEXT("colou?r"), TEXT("color"));
    TestRegex(TEXT("colou?r"), TEXT("colour"));
    TestRegex(TEXT("colou?r"), TEXT("colouur"));
    TestRegex(TEXT("a[0-9]b"), TEXT("a7b"));
    TestRegex(TEXT("a[0-9]b"), TEXT("ab"));
    TestRegex(TEXT("a[^0-9]b"), TEXT("axb"));

    ConsolePrint(TEXT("=== END SELF TEST ===\n"));
}
