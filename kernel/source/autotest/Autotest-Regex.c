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


    Regular Expression - Unit Tests

\************************************************************************/

#include "../../include/Base.h"
#include "../../include/Log.h"
#include "../../include/Regex.h"
#include "../../include/String.h"

/************************************************************************/

/**
 * @brief Helper function to test a single regex pattern against text.
 *
 * Tests both regex matching (full text match) and searching (partial match).
 * Logs detailed results including match status and search span positions.
 *
 * @param Pattern Regular expression pattern to test
 * @param Text Input text to match against
 * @param ExpectedMatch Expected result for full match test
 * @param ExpectedSearch Expected result for search test
 * @return TRUE if both match and search results meet expectations
 */
static BOOL TestSingleRegex(LPCSTR Pattern, LPCSTR Text, BOOL ExpectedMatch, BOOL ExpectedSearch) {
    REGEX Rx;
    BOOL TestPassed = TRUE;
    
    // Compile the regex pattern
    BOOL CompileOk = RegexCompile(Pattern, &Rx);
    if (!CompileOk) {
        KernelLogText(LOG_ERROR, TEXT("[TestRegex] Regex compile failed: %s"), Pattern);
        return FALSE;
    }

    // Test full match
    BOOL Match = RegexMatch(&Rx, Text);
    if (Match != ExpectedMatch) {
        KernelLogText(LOG_ERROR, TEXT("[TestRegex] Match test failed: pattern=\"%s\", text=\"%s\", expected=%s, got=%s"), 
                      Pattern, Text, ExpectedMatch ? TEXT("TRUE") : TEXT("FALSE"), Match ? TEXT("TRUE") : TEXT("FALSE"));
        TestPassed = FALSE;
    }

    // Test search with position tracking
    U32 Start = 0, End = 0;
    BOOL Search = RegexSearch(&Rx, Text, &Start, &End);
    if (Search != ExpectedSearch) {
        KernelLogText(LOG_ERROR, TEXT("[TestRegex] Search test failed: pattern=\"%s\", text=\"%s\", expected=%s, got=%s"), 
                      Pattern, Text, ExpectedSearch ? TEXT("TRUE") : TEXT("FALSE"), Search ? TEXT("TRUE") : TEXT("FALSE"));
        TestPassed = FALSE;
    }

    // Log successful test details
    if (TestPassed) {
        KernelLogText(LOG_DEBUG, TEXT("[TestRegex] OK: Pattern=\"%s\", Text=\"%s\", Match=%s, Search=%s"), 
                      Pattern, Text, Match ? TEXT("YES") : TEXT("NO"), Search ? TEXT("YES") : TEXT("NO"));
        if (Search) {
            KernelLogText(LOG_DEBUG, TEXT("[TestRegex]   Search span: %u..%u"), Start, End);
        }
    }

    return TestPassed;
}

/**
 * @brief Comprehensive unit test for regular expression functionality.
 *
 * Tests various regex patterns including character classes, quantifiers,
 * anchors, and special characters. Validates both full matching and
 * substring searching capabilities of the regex engine.
 *
 * @return TRUE if all regex tests pass, FALSE if any test fails
 */
BOOL TestRegex(void) {
    BOOL AllTestsPassed = TRUE;
    
    KernelLogText(LOG_DEBUG, TEXT("[TestRegex] Starting regex engine tests"));

    // Test 1: Valid identifier pattern (should match)
    if (!TestSingleRegex(TEXT("^[A-Za-z_][A-Za-z0-9_]*$"), TEXT("Hello_123"), TRUE, TRUE)) {
        AllTestsPassed = FALSE;
    }

    // Test 2: Invalid identifier (starts with number - should not match)
    if (!TestSingleRegex(TEXT("^[A-Za-z_][A-Za-z0-9_]*$"), TEXT("123Oops"), FALSE, FALSE)) {
        AllTestsPassed = FALSE;
    }

    // Test 3: Wildcard character '.' (should match any single character)
    if (!TestSingleRegex(TEXT("^h.llo$"), TEXT("hello"), TRUE, TRUE)) {
        AllTestsPassed = FALSE;
    }
    if (!TestSingleRegex(TEXT("^h.llo$"), TEXT("hallo"), TRUE, TRUE)) {
        AllTestsPassed = FALSE;
    }
    if (!TestSingleRegex(TEXT("^h.llo$"), TEXT("hxllo"), TRUE, TRUE)) {
        AllTestsPassed = FALSE;
    }

    // Test 4: Kleene star '*' quantifier (zero or more)
    if (!TestSingleRegex(TEXT("ab*c"), TEXT("ac"), TRUE, TRUE)) {      // Zero 'b's
        AllTestsPassed = FALSE;
    }
    if (!TestSingleRegex(TEXT("ab*c"), TEXT("abc"), TRUE, TRUE)) {     // One 'b'
        AllTestsPassed = FALSE;
    }
    if (!TestSingleRegex(TEXT("ab*c"), TEXT("abbbc"), TRUE, TRUE)) {   // Multiple 'b's
        AllTestsPassed = FALSE;
    }

    // Test 5: Optional '?' quantifier (zero or one)
    if (!TestSingleRegex(TEXT("colou?r"), TEXT("color"), TRUE, TRUE)) {   // No 'u'
        AllTestsPassed = FALSE;
    }
    if (!TestSingleRegex(TEXT("colou?r"), TEXT("colour"), TRUE, TRUE)) {  // One 'u'
        AllTestsPassed = FALSE;
    }
    if (!TestSingleRegex(TEXT("colou?r"), TEXT("colouur"), FALSE, FALSE)) { // Two 'u's (search finds "colour")
        AllTestsPassed = FALSE;
    }

    // Test 6: Character class [0-9] (should match any digit)
    if (!TestSingleRegex(TEXT("a[0-9]b"), TEXT("a7b"), TRUE, TRUE)) {
        AllTestsPassed = FALSE;
    }
    if (!TestSingleRegex(TEXT("a[0-9]b"), TEXT("ab"), FALSE, FALSE)) {    // Missing digit
        AllTestsPassed = FALSE;
    }

    // Test 7: Negated character class [^0-9] (should match any non-digit)
    if (!TestSingleRegex(TEXT("a[^0-9]b"), TEXT("axb"), TRUE, TRUE)) {
        AllTestsPassed = FALSE;
    }

    return AllTestsPassed;
}
