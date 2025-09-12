
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

#include "../include/Base.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Autotest.h"

/************************************************************************/

// Test function signature
typedef BOOL (*TestFunction)(void);

// Test registry structure
typedef struct {
    LPCSTR Name;        // Test name for logging
    TestFunction Func;  // Test function pointer
} TESTENTRY;

/************************************************************************/

// Test functions are declared in Autotest.h

// Test registry - add new tests here
static TESTENTRY TestRegistry[] = {
    { TEXT("TestCopyStack"), TestCopyStack },
    { TEXT("TestRegex"), TestRegex },
    { TEXT("TestI386Disassembler"), TestI386Disassembler },
    // Add new tests here following the same pattern
    // { TEXT("TestName"), TestFunctionName },
    { NULL, NULL }  // End marker
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
 * @return TRUE if test passed, FALSE if failed
 */
static BOOL RunSingleTest(const TESTENTRY* Entry) {
    BOOL Result;
    
    KernelLogText(LOG_VERBOSE, TEXT("[Autotest] Running test: %s"), Entry->Name);
    
    // Run the test function
    Result = Entry->Func();
    
    if (Result) {
        KernelLogText(LOG_VERBOSE, TEXT("[Autotest] PASS: %s"), Entry->Name);
    } else {
        KernelLogText(LOG_ERROR, TEXT("[Autotest] FAIL: %s"), Entry->Name);
    }
    
    return Result;
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
    U32 TotalTests = CountTests();
    U32 PassedTests = 0;
    U32 FailedTests = 0;
    U32 Index = 0;
    BOOL AllPassed = TRUE;
    
    KernelLogText(LOG_VERBOSE, TEXT(""));
    KernelLogText(LOG_VERBOSE, TEXT("========================================"));
    KernelLogText(LOG_VERBOSE, TEXT("[Autotest] Starting EXOS Test Suite"));
    KernelLogText(LOG_VERBOSE, TEXT("[Autotest] Found %u tests to run"), TotalTests);
    KernelLogText(LOG_VERBOSE, TEXT("========================================"));
    
    // Run each test in the registry
    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        if (RunSingleTest(&TestRegistry[Index])) {
            PassedTests++;
        } else {
            FailedTests++;
            AllPassed = FALSE;
        }
    }
    
    // Print summary
    KernelLogText(LOG_VERBOSE, TEXT(""));
    KernelLogText(LOG_VERBOSE, TEXT("========================================"));
    KernelLogText(LOG_VERBOSE, TEXT("[Autotest] Test Suite Complete"));
    KernelLogText(LOG_VERBOSE, TEXT("[Autotest] Total: %u, Passed: %u, Failed: %u"), 
                  TotalTests, PassedTests, FailedTests);
    
    if (AllPassed) {
        KernelLogText(LOG_VERBOSE, TEXT("[Autotest] ALL TESTS PASSED"));
    } else {
        KernelLogText(LOG_ERROR, TEXT("[Autotest] SOME TESTS FAILED"));
    }
    
    KernelLogText(LOG_VERBOSE, TEXT("========================================"));
    KernelLogText(LOG_VERBOSE, TEXT(""));
    
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
    
    if (TestName == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[Autotest] Test name is NULL"));
        return FALSE;
    }
    
    KernelLogText(LOG_VERBOSE, TEXT("[Autotest] Looking for test: %s"), TestName);
    
    // Search for the test in the registry
    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        if (StringCompare(TestRegistry[Index].Name, TestName) == 0) {
            KernelLogText(LOG_VERBOSE, TEXT("[Autotest] Found test: %s"), TestName);
            return RunSingleTest(&TestRegistry[Index]);
        }
    }
    
    KernelLogText(LOG_ERROR, TEXT("[Autotest] Test not found: %s"), TestName);
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
    
    KernelLogText(LOG_VERBOSE, TEXT("[Autotest] Available tests (%u total):"), TotalTests);
    
    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        KernelLogText(LOG_VERBOSE, TEXT("[Autotest]   %u. %s"), Index + 1, TestRegistry[Index].Name);
    }
}
