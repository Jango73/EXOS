
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


    Autotest Manager - Unit Testing Framework

\************************************************************************/

#include "../include/Autotest.h"

#include "../include/Base.h"
#include "../include/Kernel.h"
#include "../include/Log.h"

/************************************************************************/

// Test registry structure
typedef struct {
    LPCSTR Name;        // Test name for logging
    TestFunction Func;  // Test function pointer
} TESTENTRY;

/************************************************************************/

// Test functions are declared in Autotest.h

// Test registry - add new tests here
static TESTENTRY TestRegistry[] = {
    {TEXT("TestCopyStack"), TestCopyStack},
    {TEXT("TestRegex"), TestRegex},
    {TEXT("TestI386Disassembler"), TestI386Disassembler},
    {TEXT("TestBcrypt"), TestBcrypt},
    {TEXT("TestIPv4"), TestIPv4},
    {TEXT("TestMacros"), TestMacros},
    {TEXT("TestTCP"), TestTCP},
    // Add new tests here following the same pattern
    // { TEXT("TestName"), TestFunctionName },
    {NULL, NULL}  // End marker
};

/************************************************************************/

/**
 * @brief Counts the number of registered tests.
 *
 * @return Number of tests in the registry
 */
static U32 CountTests(void) {
    U32 Count = 0;

    while (TestRegistry[Count].Name != NULL) {
        Count++;
    }

    return Count;
}

/************************************************************************/

/**
 * @brief Runs a single test and reports the result.
 *
 * @param Entry Test registry entry containing name and function pointer
 * @param Results Pointer to TEST_RESULTS structure to be filled
 */
static void RunSingleTest(const TESTENTRY* Entry, TEST_RESULTS* Results) {
    TEST_RESULTS TestResults = {0, 0};

    DEBUG(TEXT("[Autotest] Running test: %s"), Entry->Name);

    // Run the test function
    Entry->Func(&TestResults);

    // Update overall results
    Results->TestsRun += TestResults.TestsRun;
    Results->TestsPassed += TestResults.TestsPassed;

    // Log results
    DEBUG(TEXT("[Autotest] %s: %u/%u passed"), Entry->Name, TestResults.TestsPassed, TestResults.TestsRun);
}

/************************************************************************/

/**
 * @brief Runs all registered unit tests.
 *
 * This function discovers and executes all tests in the autotest registry.
 * It provides a summary of test results including pass/fail counts and
 * overall test suite status.
 *
 * @return TRUE if all tests passed, FALSE if any test failed
 */
BOOL RunAllTests(void) {
    U32 TotalTestModules = CountTests();
    U32 Index = 0;
    TEST_RESULTS OverallResults = {0, 0};
    BOOL AllPassed = TRUE;

    DEBUG(TEXT("==========================================================================="));
    DEBUG(TEXT("[Autotest] Starting Test Suite"));
    DEBUG(TEXT("[Autotest] Found %u test modules to run"), TotalTestModules);

    // Run each test in the registry
    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        RunSingleTest(&TestRegistry[Index], &OverallResults);
    }

    // Determine overall pass/fail status
    AllPassed = (OverallResults.TestsRun == OverallResults.TestsPassed);

    // Print summary
    DEBUG(TEXT("[Autotest] Test Suite Complete"));
    DEBUG(TEXT("[Autotest] Tests Run: %u, Tests Passed: %u"), OverallResults.TestsRun, OverallResults.TestsPassed);

    if (AllPassed) {
        DEBUG(TEXT("[Autotest] ALL TESTS PASSED"));
    } else {
        DEBUG(TEXT("[Autotest] SOME TESTS FAILED (%u failures)"), OverallResults.TestsRun - OverallResults.TestsPassed);
    }

    DEBUG(TEXT("==========================================================================="));

    return AllPassed;
}

/************************************************************************/

/**
 * @brief Runs a specific test by name.
 *
 * Searches the test registry for a test with the specified name and runs it.
 * Useful for debugging specific failing tests or running tests selectively.
 *
 * @param TestName Name of the test to run
 * @return TRUE if test exists and passed, FALSE if not found or failed
 */
BOOL RunSingleTestByName(LPCSTR TestName) {
    U32 Index = 0;
    TEST_RESULTS TestResults = {0, 0};

    if (TestName == NULL) {
        DEBUG(TEXT("[Autotest] Test name is NULL"));
        return FALSE;
    }

    DEBUG(TEXT("[Autotest] Looking for test: %s"), TestName);

    // Search for the test in the registry
    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        if (StringCompare(TestRegistry[Index].Name, TestName) == 0) {
            DEBUG(TEXT("[Autotest] Found test: %s"), TestName);
            RunSingleTest(&TestRegistry[Index], &TestResults);
            return (TestResults.TestsRun == TestResults.TestsPassed);
        }
    }

    DEBUG(TEXT("[Autotest] Test not found: %s"), TestName);
    return FALSE;
}

/************************************************************************/
/**
 * @brief Lists all available tests in the registry.
 *
 * Prints the names of all registered tests to the kernel log.
 * Useful for discovering what tests are available to run.
 */
void ListAllTests(void) {
    U32 TotalTests = CountTests();
    U32 Index = 0;

    (void)TotalTests;

    DEBUG(TEXT("[Autotest] Available tests (%u total):"), TotalTests);

    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        DEBUG(TEXT("[Autotest]   %u. %s"), Index + 1, TestRegistry[Index].Name);
    }
}
