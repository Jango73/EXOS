
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


    Script Engine - Unit Tests

\************************************************************************/

#include "Autotest.h"
#include "Base.h"
#include "Log.h"
#include "Script.h"
#include "String.h"

/************************************************************************/

/**
 * @brief Test simple arithmetic expression.
 *
 * This function tests basic arithmetic operations like addition.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptSimpleArithmetic(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple addition
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptSimpleArithmetic] Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("a = 1 + 2;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("a"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 3) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptSimpleArithmetic] Test 1 failed: a = %d (expected 3)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptSimpleArithmetic] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Simple subtraction
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptSimpleArithmetic] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("b = 10 - 3;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("b"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 7) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptSimpleArithmetic] Test 2 failed: b = %d (expected 7)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptSimpleArithmetic] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: Multiplication
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptSimpleArithmetic] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("c = 4 * 5;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("c"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 20) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptSimpleArithmetic] Test 3 failed: c = %d (expected 20)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptSimpleArithmetic] Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 4: Division
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptSimpleArithmetic] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("d = 20 / 4;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("d"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 5) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptSimpleArithmetic] Test 4 failed: d = %d (expected 5)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptSimpleArithmetic] Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test complex arithmetic expressions with operator precedence.
 *
 * This function tests expressions involving multiple operators and
 * proper operator precedence.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptComplexArithmetic(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Operator precedence
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComplexArithmetic] Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("a = 2 + 3 * 4;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("a"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 14) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComplexArithmetic] Test 1 failed: a = %d (expected 14)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComplexArithmetic] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Parentheses
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComplexArithmetic] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("b = (2 + 3) * 4;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("b"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 20) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComplexArithmetic] Test 2 failed: b = %d (expected 20)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComplexArithmetic] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: Using variables in expressions
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComplexArithmetic] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("x = 5; y = 10; z = x + y * 2;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("z"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 25) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComplexArithmetic] Test 3 failed: z = %d (expected 25)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComplexArithmetic] Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test comparison operators.
 *
 * This function tests all comparison operators: <, <=, >, >=, ==, !=
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptComparisons(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Less than (true)
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComparisons] Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("a = 5 < 10;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("a"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComparisons] Test 1 failed: a = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComparisons] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Greater than (false)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComparisons] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("b = 5 > 10;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("b"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 0) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComparisons] Test 2 failed: b = %d (expected 0)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComparisons] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: Equal (true)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComparisons] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("c = 10 == 10;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("c"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComparisons] Test 3 failed: c = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComparisons] Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 4: Not equal (true)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComparisons] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("d = 5 != 10;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("d"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComparisons] Test 4 failed: d = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComparisons] Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test if/else statements.
 *
 * This function tests conditional execution with if/else.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptIfElse(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple if (true condition)
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptIfElse] Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("a = 0; if (5 > 3) { a = 10; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("a"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 10) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptIfElse] Test 1 failed: a = %d (expected 10)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptIfElse] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Simple if (false condition)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptIfElse] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("b = 5; if (3 > 5) { b = 10; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("b"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 5) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptIfElse] Test 2 failed: b = %d (expected 5)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptIfElse] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: If-else (true branch)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptIfElse] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("c = 0; if (10 == 10) { c = 100; } else { c = 200; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("c"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 100) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptIfElse] Test 3 failed: c = %d (expected 100)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptIfElse] Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 4: If-else (false branch)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptIfElse] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("d = 0; if (10 != 10) { d = 100; } else { d = 200; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("d"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 200) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptIfElse] Test 4 failed: d = %d (expected 200)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptIfElse] Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test simple for loops.
 *
 * This function tests basic for loop functionality.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptSimpleForLoop(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple counting loop
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptSimpleForLoop] Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("sum = 0; for (i = 0; i < 10; i = i + 1) { sum = sum + i; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("sum"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 45) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptSimpleForLoop] Test 1 failed: sum = %d (expected 45)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptSimpleForLoop] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Loop with multiplication
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptSimpleForLoop] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("product = 1; for (j = 1; j <= 5; j = j + 1) { product = product * j; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("product"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 120) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptSimpleForLoop] Test 2 failed: product = %d (expected 120)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptSimpleForLoop] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test nested for loops.
 *
 * This function tests nested loop functionality.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptNestedForLoops(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Nested loops with counter
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptNestedForLoops] Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("count = 0; for (i = 0; i < 5; i = i + 1) { for (j = 0; j < 3; j = j + 1) { count = count + 1; } }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("count"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 15) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptNestedForLoops] Test 1 failed: count = %d (expected 15)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptNestedForLoops] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Nested loops with multiplication
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptNestedForLoops] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("result = 0; for (x = 1; x <= 3; x = x + 1) { for (y = 1; y <= 4; y = y + 1) { result = result + x * y; } }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("result"));
        // Expected: (1*1 + 1*2 + 1*3 + 1*4) + (2*1 + 2*2 + 2*3 + 2*4) + (3*1 + 3*2 + 3*3 + 3*4) = 10 + 20 + 30 = 60
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 60) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptNestedForLoops] Test 2 failed: result = %d (expected 60)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptNestedForLoops] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test array operations.
 *
 * This function tests array creation and element access.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptArrays(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple array assignment and retrieval
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptArrays] Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("arr[0] = 10; arr[1] = 20; arr[2] = 30; val = arr[1]"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("val"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 20) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptArrays] Test 1 failed: val = %d (expected 20)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptArrays] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Array with loop
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptArrays] Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("for (k = 0; k < 5; k = k + 1) { data[k] = k * 10; } result = data[3];"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("result"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 30) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptArrays] Test 2 failed: result = %d (expected 30)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptArrays] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test complex script with multiple features.
 *
 * This function tests a complex script combining loops, conditionals,
 * arrays, and arithmetic operations.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptComplex(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Fibonacci-like calculation with array
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComplex] Failed to create context"));
        return;
    }

    LPCSTR Script = TEXT(
        "fib[0] = 0;\n"
        "fib[1] = 1;\n"
        "for (n = 2; n < 10; n = n + 1) {\n"
        "  n1 = n - 1;\n"
        "  n2 = n - 2;\n"
        "  fib[n] = fib[n1] + fib[n2];\n"
        "}\n"
        "result = fib[9];"
    );

    SCRIPT_ERROR Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("result"));
        // Fibonacci sequence: 0, 1, 1, 2, 3, 5, 8, 13, 21, 34
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 34) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComplex] Test 1 failed: result = %d (expected 34)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComplex] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Complex nested loops with conditionals
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComplex] Failed to create context"));
        return;
    }

    Script = TEXT(
        "total = 0;\n"
        "for (i = 1; i <= 10; i = i + 1) {\n"
        "  for (j = 1; j <= 10; j = j + 1) {\n"
        "    prod = i * j;\n"
        "    if (prod > 20) {\n"
        "      if (prod < 30) {\n"
        "        total = total + 1;\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}"
    );

    Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("total"));
        // Count products where 20 < i*j < 30
        // This includes: 21, 22, 24, 25, 27, 28 appearing multiple times
        // (3,7), (3,8), (3,9), (4,6), (4,7), (5,5), (6,4), (7,3), (7,4), (8,3), (9,3) = 11
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 11) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComplex] Test 2 failed: total = %d (expected 11)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComplex] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: Prime number checking (simplified)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptComplex] Failed to create context"));
        return;
    }

    Script = TEXT(
        "num = 17;\n"
        "isPrime = 1;\n"
        "if (num < 2) {\n"
        "  isPrime = 0;\n"
        "} else {\n"
        "  for (i = 2; i < num; i = i + 1) {\n"
        "    div = num / i;\n"
        "    prod = div * i;\n"
        "    if (prod == num) {\n"
        "      isPrime = 0;\n"
        "    }\n"
        "  }\n"
        "}"
    );

    Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("isPrime"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptComplex] Test 3 failed: isPrime = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptComplex] Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test loop with if inside.
 *
 * This function tests combining loops with conditional statements.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptLoopWithIf(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Count even numbers
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptLoopWithIf] Failed to create context"));
        return;
    }

    LPCSTR Script = TEXT(
        "count = 0;\n"
        "for (i = 0; i < 10; i = i + 1) {\n"
        "  div = i / 2;\n"
        "  prod = div * 2;\n"
        "  if (prod == i) {\n"
        "    count = count + 1;\n"
        "  }\n"
        "}"
    );

    SCRIPT_ERROR Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("count"));
        // Even numbers from 0 to 9: 0, 2, 4, 6, 8 = 5 numbers
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 5) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptLoopWithIf] Test 1 failed: count = %d (expected 5)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptLoopWithIf] Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Sum of numbers greater than threshold
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("[TestScriptLoopWithIf] Failed to create context"));
        return;
    }

    Script = TEXT(
        "threshold = 5;\n"
        "sum = 0;\n"
        "for (i = 0; i <= 10; i = i + 1) {\n"
        "  if (i > threshold) {\n"
        "    sum = sum + i;\n"
        "  }\n"
        "}"
    );

    Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("sum"));
        // Sum of 6 + 7 + 8 + 9 + 10 = 40
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 40) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("[TestScriptLoopWithIf] Test 2 failed: sum = %d (expected 40)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("[TestScriptLoopWithIf] Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Main Script test function that runs all Script unit tests.
 *
 * This function coordinates all Script unit tests and aggregates their results.
 * It tests arithmetic, comparisons, control flow, loops, arrays, and complex
 * script combinations.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScript(TEST_RESULTS* Results) {
    TEST_RESULTS SubResults;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Run simple arithmetic tests
    TestScriptSimpleArithmetic(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run complex arithmetic tests
    TestScriptComplexArithmetic(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run comparison tests
    TestScriptComparisons(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run if/else tests
    TestScriptIfElse(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run simple for loop tests
    TestScriptSimpleForLoop(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run nested for loop tests
    TestScriptNestedForLoops(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run array tests
    TestScriptArrays(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run loop with if tests
    TestScriptLoopWithIf(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run complex script tests
    TestScriptComplex(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;
}
