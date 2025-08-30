
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
        ConsolePrint("Regex compile failed: %s\n", Pattern);
        return;
    }

    BOOL Match = RegexMatch(&Rx, Text);
    U32 Start = 0, End = 0;
    BOOL Search = RegexSearch(&Rx, Text, &Start, &End);

    ConsolePrint("Pattern: \"%s\"\n", Pattern);
    ConsolePrint("Text   : \"%s\"\n", Text);
    ConsolePrint("Match? : %s\n", Match ? "YES" : "NO");
    if (Search) {
        ConsolePrint("Search : YES (span %u..%u)\n", Start, End);
    } else {
        ConsolePrint("Search : NO\n");
    }
    ConsolePrint("\n");
}

void RegexSelfTest(void) {
    ConsolePrint("=== REGEX SELF TEST ===\n");

    TestRegex("^[A-Za-z_][A-Za-z0-9_]*$", "Hello_123");
    TestRegex("^[A-Za-z_][A-Za-z0-9_]*$", "123Oops");
    TestRegex("^h.llo$", "hello");
    TestRegex("^h.llo$", "hallo");
    TestRegex("^h.llo$", "hxllo");
    TestRegex("ab*c", "ac");
    TestRegex("ab*c", "abc");
    TestRegex("ab*c", "abbbc");
    TestRegex("colou?r", "color");
    TestRegex("colou?r", "colour");
    TestRegex("colou?r", "colouur");
    TestRegex("a[0-9]b", "a7b");
    TestRegex("a[0-9]b", "ab");
    TestRegex("a[^0-9]b", "axb");

    ConsolePrint("=== END SELF TEST ===\n");
}
