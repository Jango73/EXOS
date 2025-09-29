
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


    Script Engine - Phase 1: Variables and Expressions

\************************************************************************/

#include "../include/Base.h"
#include "../include/Heap.h"
#include "../include/List.h"
#include "../include/Log.h"
#include "../include/String.h"
#include "../include/Script.h"

/************************************************************************/

static U32 ScriptHashVariable(LPCSTR Name);
static void ScriptFreeVariable(LPSCRIPT_VARIABLE Variable);
static void ScriptInitParser(LPSCRIPT_PARSER Parser, LPCSTR Input, LPSCRIPT_VAR_TABLE Variables, LPSCRIPT_CALLBACKS Callbacks, LPSCRIPT_SCOPE CurrentScope);
static void ScriptNextToken(LPSCRIPT_PARSER Parser);
static F32 ScriptParseExpression(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static F32 ScriptParseComparison(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static F32 ScriptParseTerm(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static F32 ScriptParseFactor(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static SCRIPT_ERROR ScriptParseAssignment(LPSCRIPT_PARSER Parser);
static SCRIPT_ERROR ScriptParseStatement(LPSCRIPT_PARSER Parser);
static SCRIPT_ERROR ScriptParseBlock(LPSCRIPT_PARSER Parser);
static SCRIPT_ERROR ScriptParseIfStatement(LPSCRIPT_PARSER Parser);
static SCRIPT_ERROR ScriptParseForStatement(LPSCRIPT_PARSER Parser);
static SCRIPT_ERROR ScriptExecuteLine(LPSCRIPT_CONTEXT Context, LPCSTR Line);
static BOOL ScriptIsKeyword(LPCSTR Str);

/************************************************************************/

/**
 * @brief Create a new script context with callback bindings.
 * @param Callbacks Pointer to callback structure for external integration
 * @return Pointer to new script context or NULL on failure
 */
LPSCRIPT_CONTEXT ScriptCreateContext(LPSCRIPT_CALLBACKS Callbacks) {

    LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)HeapAlloc(sizeof(SCRIPT_CONTEXT));
    if (Context == NULL) {
        DEBUG(TEXT("[ScriptCreateContext] Failed to allocate context"));
        return NULL;
    }

    MemorySet(Context, 0, sizeof(SCRIPT_CONTEXT));

    // Initialize global scope
    Context->GlobalScope = ScriptCreateScope(NULL);
    if (Context->GlobalScope == NULL) {
        DEBUG(TEXT("[ScriptCreateContext] Failed to create global scope"));
        ScriptDestroyContext(Context);
        return NULL;
    }
    Context->CurrentScope = Context->GlobalScope;

    if (Callbacks) {
        Context->Callbacks = *Callbacks;
    }

    Context->ErrorCode = SCRIPT_OK;

    return Context;
}

/************************************************************************/

/**
 * @brief Destroy a script context and free all resources.
 * @param Context Script context to destroy
 */
void ScriptDestroyContext(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL) return;

    // Free global scope and all child scopes
    if (Context->GlobalScope) {
        ScriptDestroyScope(Context->GlobalScope);
    }

    HeapFree(Context);
}

/************************************************************************/

/**
 * @brief Execute a script (can contain multiple lines).
 * @param Context Script context to use
 * @param Script Script text to execute (may contain newlines)
 * @return Script error code
 */
SCRIPT_ERROR ScriptExecute(LPSCRIPT_CONTEXT Context, LPCSTR Script) {
    if (Context == NULL || Script == NULL) {
        DEBUG(TEXT("[ScriptExecute] NULL parameters"));
        return SCRIPT_ERROR_SYNTAX;
    }

    Context->ErrorCode = SCRIPT_OK;
    Context->ErrorMessage[0] = STR_NULL;

    // Split script into lines and execute each
    STR Line[1024];
    U32 LineStart = 0;
    UNUSED(LineStart);
    U32 ScriptPos = 0;

    while (Script[ScriptPos] != STR_NULL) {
        U32 LinePos = 0;

        // Extract one line
        while (Script[ScriptPos] != STR_NULL && Script[ScriptPos] != '\n' && Script[ScriptPos] != '\r') {
            if (LinePos < sizeof(Line) - 1) {
                Line[LinePos++] = Script[ScriptPos];
            }
            ScriptPos++;
        }

        Line[LinePos] = STR_NULL;

        // Skip newline characters
        while (Script[ScriptPos] == '\n' || Script[ScriptPos] == '\r') {
            ScriptPos++;
        }

        // Execute line if not empty
        if (StringLength(Line) > 0) {
            SCRIPT_ERROR Error = ScriptExecuteLine(Context, Line);
            if (Error != SCRIPT_OK) {
                Context->ErrorCode = Error;
                return Error;
            }
        }
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Check if a line contains script syntax.
 * @param Line Line to check
 * @return TRUE if line contains script syntax
 */
BOOL ScriptIsScriptSyntax(LPCSTR Line) {
    if (Line == NULL) return FALSE;

    // Skip whitespace
    while (*Line == ' ' || *Line == '\t') Line++;
    if (*Line == STR_NULL) return FALSE;

    // Check for control flow keywords at the beginning
    if ((Line[0] == 'i' && Line[1] == 'f' && (Line[2] == ' ' || Line[2] == '(')) ||
        (Line[0] == 'f' && Line[1] == 'o' && Line[2] == 'r' && (Line[3] == ' ' || Line[3] == '(')) ||
        (Line[0] == 'e' && Line[1] == 'l' && Line[2] == 's' && Line[3] == 'e')) {
        return TRUE;
    }

    // Look for braces (block syntax)
    LPCSTR Pos = Line;
    while (*Pos != STR_NULL) {
        if (*Pos == '{' || *Pos == '}') {
            return TRUE;
        }
        if (*Pos == '[' || *Pos == ']') {
            return TRUE;
        }
        if (*Pos == '=' && Pos > Line && Pos[-1] != '=' && Pos[1] != '=') {
            return TRUE;
        }
        // Look for comparison operators
        if ((*Pos == '<' || *Pos == '>' || *Pos == '!') &&
            (Pos[1] == '=' || (*Pos != '!' && Pos[1] != STR_NULL))) {
            return TRUE;
        }
        if (*Pos == '=' && Pos[1] == '=') {
            return TRUE;
        }
        Pos++;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Set a variable value in the script context.
 * @param Context Script context
 * @param Name Variable name
 * @param Type Variable type
 * @param Value Variable value
 * @return Pointer to variable or NULL on failure
 */
LPSCRIPT_VARIABLE ScriptSetVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value) {
    if (Context == NULL || Name == NULL) return NULL;

    return ScriptSetVariableInScope(Context->CurrentScope, Name, Type, Value);
}

/************************************************************************/

/**
 * @brief Get a variable from the script context.
 * @param Context Script context
 * @param Name Variable name
 * @return Pointer to variable or NULL if not found
 */
LPSCRIPT_VARIABLE ScriptGetVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name) {
    if (Context == NULL || Name == NULL) return NULL;

    return ScriptFindVariableInScope(Context->CurrentScope, Name, TRUE);
}

/************************************************************************/

/**
 * @brief Delete a variable from the script context.
 * @param Context Script context
 * @param Name Variable name to delete
 */
void ScriptDeleteVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name) {
    if (Context == NULL || Name == NULL || Context->CurrentScope == NULL) return;

    U32 Hash = ScriptHashVariable(Name);
    LPLIST Bucket = Context->CurrentScope->Buckets[Hash];

    // Only delete from current scope, not parent scopes
    for (LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)Bucket->First; Variable; Variable = (LPSCRIPT_VARIABLE)Variable->Next) {
        if (StringCompare(Variable->Name, Name) == 0) {
            ListRemove(Bucket, Variable);
            ScriptFreeVariable(Variable);
            Context->CurrentScope->Count--;
            break;
        }
    }
}

/************************************************************************/

/**
 * @brief Get the last error code from script execution.
 * @param Context Script context
 * @return Error code
 */
SCRIPT_ERROR ScriptGetLastError(LPSCRIPT_CONTEXT Context) {
    return Context ? Context->ErrorCode : SCRIPT_ERROR_SYNTAX;
}

/************************************************************************/

/**
 * @brief Get the last error message from script execution.
 * @param Context Script context
 * @return Error message string
 */
LPCSTR ScriptGetErrorMessage(LPSCRIPT_CONTEXT Context) {
    return Context ? Context->ErrorMessage : TEXT("Invalid context");
}

/************************************************************************/

/**
 * @brief Hash function for variable names.
 * @param Name Variable name to hash
 * @return Hash value
 */
static U32 ScriptHashVariable(LPCSTR Name) {
    U32 Hash = 5381;
    while (*Name) {
        Hash = ((Hash << 5) + Hash) + *Name++;
    }
    return Hash % SCRIPT_VAR_HASH_SIZE;
}

/************************************************************************/

/**
 * @brief Find a variable in the hash table.
 * @param Table Variable table to search
 * @param Name Variable name to find
 * @return Pointer to variable or NULL if not found
 */

/************************************************************************/

/**
 * @brief Free a variable and its resources.
 * @param Variable Variable to free
 */
static void ScriptFreeVariable(LPSCRIPT_VARIABLE Variable) {
    if (Variable == NULL) return;

    if (Variable->Type == SCRIPT_VAR_STRING && Variable->Value.String) {
        HeapFree(Variable->Value.String);
    } else if (Variable->Type == SCRIPT_VAR_ARRAY && Variable->Value.Array) {
        ScriptDestroyArray(Variable->Value.Array);
    }

    HeapFree(Variable);
}

/************************************************************************/

/**
 * @brief Execute a single line of script (may contain multiple statements separated by semicolons).
 * @param Context Script context
 * @param Line Line to execute
 * @return Script error code
 */
static SCRIPT_ERROR ScriptExecuteLine(LPSCRIPT_CONTEXT Context, LPCSTR Line) {
    SCRIPT_PARSER Parser;
    ScriptInitParser(&Parser, Line, &Context->Variables, &Context->Callbacks, Context->CurrentScope);

    // Parse all statements on this line until EOF
    while (Parser.CurrentToken.Type != TOKEN_EOF) {
        SCRIPT_ERROR Error = ScriptParseStatement(&Parser);
        if (Error != SCRIPT_OK) {
            StringCopy(Context->ErrorMessage, TEXT("Syntax error"));
            Context->ErrorCode = Error;
            return Error;
        }

        // Semicolon is mandatory to terminate statement
        if (Parser.CurrentToken.Type != TOKEN_SEMICOLON && Parser.CurrentToken.Type != TOKEN_EOF) {
            DEBUG(TEXT("[ScriptExecuteLine] Expected semicolon, got token type %d"), Parser.CurrentToken.Type);
            StringCopy(Context->ErrorMessage, TEXT("Expected semicolon"));
            Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
            return SCRIPT_ERROR_SYNTAX;
        }

        // Skip semicolon if present
        if (Parser.CurrentToken.Type == TOKEN_SEMICOLON) {
            ScriptNextToken(&Parser);
        }
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Initialize a script parser.
 * @param Parser Parser to initialize
 * @param Input Input string to parse
 * @param Variables Variable table
 * @param Callbacks Callback functions
 */
static void ScriptInitParser(LPSCRIPT_PARSER Parser, LPCSTR Input, LPSCRIPT_VAR_TABLE Variables, LPSCRIPT_CALLBACKS Callbacks, LPSCRIPT_SCOPE CurrentScope) {
    Parser->Input = Input;
    Parser->Position = 0;
    Parser->Variables = Variables;
    Parser->Callbacks = Callbacks;
    Parser->CurrentScope = CurrentScope;

    ScriptNextToken(Parser);
}

/************************************************************************/

/**
 * @brief Get the next token from input.
 * @param Parser Parser state
 */
static void ScriptNextToken(LPSCRIPT_PARSER Parser) {
    LPCSTR Input = Parser->Input;
    U32* Pos = &Parser->Position;

    // Skip whitespace
    while (Input[*Pos] == ' ' || Input[*Pos] == '\t') (*Pos)++;

    Parser->CurrentToken.Position = *Pos;

    if (Input[*Pos] == STR_NULL) {
        Parser->CurrentToken.Type = TOKEN_EOF;
        return;
    }

    STR Ch = Input[*Pos];

    if (Ch >= '0' && Ch <= '9') {
        // Number
        Parser->CurrentToken.Type = TOKEN_NUMBER;
        U32 Start = *Pos;
        while ((Input[*Pos] >= '0' && Input[*Pos] <= '9') || Input[*Pos] == '.') {
            (*Pos)++;
        }

        U32 Len = *Pos - Start;
        if (Len >= MAX_TOKEN_LENGTH) Len = MAX_TOKEN_LENGTH - 1;

        MemoryCopy(Parser->CurrentToken.Value, &Input[Start], Len);
        Parser->CurrentToken.Value[Len] = STR_NULL;
        Parser->CurrentToken.NumValue = (F32)StringToU32(Parser->CurrentToken.Value);

    } else if ((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || Ch == '_') {
        // Identifier
        Parser->CurrentToken.Type = TOKEN_IDENTIFIER;
        U32 Start = *Pos;
        while ((Input[*Pos] >= 'a' && Input[*Pos] <= 'z') ||
               (Input[*Pos] >= 'A' && Input[*Pos] <= 'Z') ||
               (Input[*Pos] >= '0' && Input[*Pos] <= '9') ||
               Input[*Pos] == '_') {
            (*Pos)++;
        }

        U32 Len = *Pos - Start;
        if (Len >= MAX_TOKEN_LENGTH) Len = MAX_TOKEN_LENGTH - 1;

        MemoryCopy(Parser->CurrentToken.Value, &Input[Start], Len);
        Parser->CurrentToken.Value[Len] = STR_NULL;

        // Check if this is a keyword
        if (ScriptIsKeyword(Parser->CurrentToken.Value)) {
            if (StringCompare(Parser->CurrentToken.Value, TEXT("if")) == 0) {
                Parser->CurrentToken.Type = TOKEN_IF;
            } else if (StringCompare(Parser->CurrentToken.Value, TEXT("else")) == 0) {
                Parser->CurrentToken.Type = TOKEN_ELSE;
            } else if (StringCompare(Parser->CurrentToken.Value, TEXT("for")) == 0) {
                Parser->CurrentToken.Type = TOKEN_FOR;
            }
        }

    } else if (Ch == '"') {
        // String
        Parser->CurrentToken.Type = TOKEN_STRING;
        (*Pos)++; // Skip opening quote
        U32 Start = *Pos;
        while (Input[*Pos] != STR_NULL && Input[*Pos] != '"') {
            (*Pos)++;
        }

        U32 Len = *Pos - Start;
        if (Len >= MAX_TOKEN_LENGTH) Len = MAX_TOKEN_LENGTH - 1;

        MemoryCopy(Parser->CurrentToken.Value, &Input[Start], Len);
        Parser->CurrentToken.Value[Len] = STR_NULL;

        if (Input[*Pos] == '"') (*Pos)++; // Skip closing quote

    } else if (Ch == '\'') {
        // String
        Parser->CurrentToken.Type = TOKEN_STRING;
        (*Pos)++; // Skip opening quote
        U32 Start = *Pos;
        while (Input[*Pos] != STR_NULL && Input[*Pos] != '\'') {
            (*Pos)++;
        }

        U32 Len = *Pos - Start;
        if (Len >= MAX_TOKEN_LENGTH) Len = MAX_TOKEN_LENGTH - 1;

        MemoryCopy(Parser->CurrentToken.Value, &Input[Start], Len);
        Parser->CurrentToken.Value[Len] = STR_NULL;

        if (Input[*Pos] == '\'') (*Pos)++; // Skip closing quote

    } else if (Ch == '(' || Ch == ')') {
        Parser->CurrentToken.Type = (Ch == '(') ? TOKEN_LPAREN : TOKEN_RPAREN;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == '[' || Ch == ']') {
        Parser->CurrentToken.Type = (Ch == '[') ? TOKEN_LBRACKET : TOKEN_RBRACKET;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == ';') {
        Parser->CurrentToken.Type = TOKEN_SEMICOLON;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == '{' || Ch == '}') {
        Parser->CurrentToken.Type = (Ch == '{') ? TOKEN_LBRACE : TOKEN_RBRACE;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == '<' || Ch == '>' || Ch == '!') {
        // Comparison operators: <, <=, >, >=, !=
        Parser->CurrentToken.Type = TOKEN_COMPARISON;
        Parser->CurrentToken.Value[0] = Ch;
        (*Pos)++;

        // Check for two-character operators
        if ((Ch == '<' && Input[*Pos] == '=') ||
            (Ch == '>' && Input[*Pos] == '=') ||
            (Ch == '!' && Input[*Pos] == '=')) {
            Parser->CurrentToken.Value[1] = Input[*Pos];
            Parser->CurrentToken.Value[2] = STR_NULL;
            (*Pos)++;
        } else {
            Parser->CurrentToken.Value[1] = STR_NULL;
        }

    } else if (Ch == '=') {
        // Handle = and == separately
        Parser->CurrentToken.Value[0] = Ch;
        (*Pos)++;

        if (Input[*Pos] == '=') {
            // == is comparison
            Parser->CurrentToken.Type = TOKEN_COMPARISON;
            Parser->CurrentToken.Value[1] = Input[*Pos];
            Parser->CurrentToken.Value[2] = STR_NULL;
            (*Pos)++;
        } else {
            // single = is operator (assignment)
            Parser->CurrentToken.Type = TOKEN_OPERATOR;
            Parser->CurrentToken.Value[1] = STR_NULL;
        }

    } else {
        // Operator
        Parser->CurrentToken.Type = TOKEN_OPERATOR;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;
    }
}

/************************************************************************/

/**
 * @brief Parse assignment statement.
 * @param Parser Parser state
 * @return Script error code
 */
static SCRIPT_ERROR ScriptParseAssignment(LPSCRIPT_PARSER Parser) {
    if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
        DEBUG(TEXT("[ScriptParseAssignment] Expected identifier, got type %d"), Parser->CurrentToken.Type);
        return SCRIPT_ERROR_SYNTAX;
    }

    STR VarName[MAX_VAR_NAME];
    StringCopy(VarName, Parser->CurrentToken.Value);

    ScriptNextToken(Parser);

    // Check for array access
    BOOL IsArrayAccess = FALSE;
    U32 ArrayIndex = 0;
    if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
        IsArrayAccess = TRUE;
        ScriptNextToken(Parser);

        // Parse array index
        if (Parser->CurrentToken.Type == TOKEN_NUMBER) {
            ArrayIndex = (U32)Parser->CurrentToken.NumValue;
            ScriptNextToken(Parser);
        } else {
            DEBUG(TEXT("[ScriptParseAssignment] Expected number for array index, got type %d"), Parser->CurrentToken.Type);
            return SCRIPT_ERROR_SYNTAX;
        }

        if (Parser->CurrentToken.Type != TOKEN_RBRACKET) {
            DEBUG(TEXT("[ScriptParseAssignment] Expected ], got type %d"), Parser->CurrentToken.Type);
            return SCRIPT_ERROR_SYNTAX;
        }
        ScriptNextToken(Parser);
    }

    if (Parser->CurrentToken.Type != TOKEN_OPERATOR || Parser->CurrentToken.Value[0] != '=') {
        DEBUG(TEXT("[ScriptParseAssignment] Expected =, got type %d value '%s'"), Parser->CurrentToken.Type, Parser->CurrentToken.Value);
        return SCRIPT_ERROR_SYNTAX;
    }

    ScriptNextToken(Parser);

    SCRIPT_ERROR Error = SCRIPT_OK;
    F32 Value = ScriptParseComparison(Parser, &Error);

    if (Error == SCRIPT_OK) {
        SCRIPT_VAR_VALUE VarValue;
        SCRIPT_VAR_TYPE VarType;

        // Check if value is a pure integer (no fractional part)
        if (Value == (F32)(I32)Value) {
            VarValue.Integer = (I32)Value;
            VarType = SCRIPT_VAR_INTEGER;
        } else {
            VarValue.Float = Value;
            VarType = SCRIPT_VAR_FLOAT;
        }

        // Get context from variables pointer using container_of-like technique
        LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)((U8*)Parser->Variables - ((U8*)&((LPSCRIPT_CONTEXT)0)->Variables - (U8*)0));

        if (IsArrayAccess) {
            // Set array element
            if (ScriptSetArrayElement(Context, VarName, ArrayIndex, VarType, VarValue) == NULL) {
                Error = SCRIPT_ERROR_SYNTAX;
            }
        } else {
            // Set regular variable in current scope
            if (ScriptSetVariableInScope(Parser->CurrentScope, VarName, VarType, VarValue) == NULL) {
                Error = SCRIPT_ERROR_SYNTAX;
            }
        }
    }

    return Error;
}

/************************************************************************/

/**
 * @brief Parse comparison operators.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return Comparison result (1.0 for true, 0.0 for false)
 */
static F32 ScriptParseComparison(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    F32 Left = ScriptParseExpression(Parser, Error);
    if (*Error != SCRIPT_OK) return 0.0f;

    while (Parser->CurrentToken.Type == TOKEN_COMPARISON) {
        STR Op[3];
        StringCopy(Op, Parser->CurrentToken.Value);
        ScriptNextToken(Parser);

        F32 Right = ScriptParseExpression(Parser, Error);
        if (*Error != SCRIPT_OK) return 0.0f;

        if (StringCompare(Op, TEXT("<")) == 0) {
            Left = (Left < Right) ? 1.0f : 0.0f;
        } else if (StringCompare(Op, TEXT("<=")) == 0) {
            Left = (Left <= Right) ? 1.0f : 0.0f;
        } else if (StringCompare(Op, TEXT(">")) == 0) {
            Left = (Left > Right) ? 1.0f : 0.0f;
        } else if (StringCompare(Op, TEXT(">=")) == 0) {
            Left = (Left >= Right) ? 1.0f : 0.0f;
        } else if (StringCompare(Op, TEXT("==")) == 0) {
            Left = (Left == Right) ? 1.0f : 0.0f;
        } else if (StringCompare(Op, TEXT("!=")) == 0) {
            Left = (Left != Right) ? 1.0f : 0.0f;
        }
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Parse expression (addition/subtraction).
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return Expression value
 */
static F32 ScriptParseExpression(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    F32 Result = ScriptParseTerm(Parser, Error);
    if (*Error != SCRIPT_OK) return 0.0f;

    while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
           (Parser->CurrentToken.Value[0] == '+' || Parser->CurrentToken.Value[0] == '-')) {
        STR Op = Parser->CurrentToken.Value[0];
        ScriptNextToken(Parser);

        F32 Right = ScriptParseTerm(Parser, Error);
        if (*Error != SCRIPT_OK) return 0.0f;

        if (Op == '+') {
            Result += Right;
        } else {
            Result -= Right;
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Parse term (multiplication/division).
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return Term value
 */
static F32 ScriptParseTerm(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    F32 Result = ScriptParseFactor(Parser, Error);
    if (*Error != SCRIPT_OK) return 0.0f;

    while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
           (Parser->CurrentToken.Value[0] == '*' || Parser->CurrentToken.Value[0] == '/')) {
        STR Op = Parser->CurrentToken.Value[0];
        ScriptNextToken(Parser);

        F32 Right = ScriptParseFactor(Parser, Error);
        if (*Error != SCRIPT_OK) return 0.0f;

        if (Op == '*') {
            Result *= Right;
        } else {
            if (Right == 0.0f) {
                *Error = SCRIPT_ERROR_DIVISION_BY_ZERO;
                return 0.0f;
            }
            Result /= Right;
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Parse factor (numbers, variables, parentheses).
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return Factor value
 */
static F32 ScriptParseFactor(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type == TOKEN_NUMBER) {
        F32 Value = Parser->CurrentToken.NumValue;
        ScriptNextToken(Parser);
        return Value;
    }

    if (Parser->CurrentToken.Type == TOKEN_IDENTIFIER) {
        STR VarName[MAX_VAR_NAME];
        StringCopy(VarName, Parser->CurrentToken.Value);

        ScriptNextToken(Parser);

        // Check for function call
        if (Parser->CurrentToken.Type == TOKEN_LPAREN) {
            ScriptNextToken(Parser);

            // Parse string argument
            if (Parser->CurrentToken.Type != TOKEN_STRING) {
                DEBUG(TEXT("[ScriptParseFactor] Expected STRING, got type %d"), Parser->CurrentToken.Type);
                *Error = SCRIPT_ERROR_SYNTAX;
                return 0.0f;
            }

            STR Argument[MAX_PATH_NAME];
            StringCopy(Argument, Parser->CurrentToken.Value);
            ScriptNextToken(Parser);

            if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
                DEBUG(TEXT("[ScriptParseFactor] Expected RPAREN, got type %d"), Parser->CurrentToken.Type);
                *Error = SCRIPT_ERROR_SYNTAX;
                return 0.0f;
            }
            ScriptNextToken(Parser);

            // Call function via callback if available
            if (Parser->Callbacks && Parser->Callbacks->CallFunction) {
                U32 Result = Parser->Callbacks->CallFunction(VarName, Argument, Parser->Callbacks->UserData);
                return (F32)Result;
            } else {
                *Error = SCRIPT_ERROR_SYNTAX;
                return 0.0f;
            }
        }
        // Check for array access
        else if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
            ScriptNextToken(Parser);

            // Parse array index - only allow numbers or variables for now to avoid recursion
            F32 IndexValue = 0.0f;
            if (Parser->CurrentToken.Type == TOKEN_NUMBER) {
                IndexValue = Parser->CurrentToken.NumValue;
                ScriptNextToken(Parser);
            } else if (Parser->CurrentToken.Type == TOKEN_IDENTIFIER) {
                // Look up variable value for index
                LPSCRIPT_VARIABLE IndexVar = ScriptFindVariableInScope(Parser->CurrentScope, Parser->CurrentToken.Value, TRUE);
                if (IndexVar == NULL) {
                    *Error = SCRIPT_ERROR_UNDEFINED_VAR;
                    return 0.0f;
                }
                if (IndexVar->Type == SCRIPT_VAR_INTEGER) {
                    IndexValue = (F32)IndexVar->Value.Integer;
                } else if (IndexVar->Type == SCRIPT_VAR_FLOAT) {
                    IndexValue = IndexVar->Value.Float;
                } else {
                    *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    return 0.0f;
                }
                ScriptNextToken(Parser);
            } else {
                *Error = SCRIPT_ERROR_SYNTAX;
                return 0.0f;
            }

            U32 ArrayIndex = (U32)IndexValue;

            if (Parser->CurrentToken.Type != TOKEN_RBRACKET) {
                *Error = SCRIPT_ERROR_SYNTAX;
                return 0.0f;
            }
            ScriptNextToken(Parser);

            // Get context from variables pointer
            LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)((U8*)Parser->Variables - ((U8*)&((LPSCRIPT_CONTEXT)0)->Variables - (U8*)0));
            LPSCRIPT_VARIABLE ElementVar = ScriptGetArrayElement(Context, VarName, ArrayIndex);

            if (ElementVar == NULL) {
                *Error = SCRIPT_ERROR_UNDEFINED_VAR;
                return 0.0f;
            }

            F32 Result = 0.0f;
            if (ElementVar->Type == SCRIPT_VAR_INTEGER) {
                Result = (F32)ElementVar->Value.Integer;
            } else if (ElementVar->Type == SCRIPT_VAR_FLOAT) {
                Result = ElementVar->Value.Float;
            } else {
                *Error = SCRIPT_ERROR_TYPE_MISMATCH;
            }

            // Free temporary variable
            HeapFree(ElementVar);
            return Result;

        } else {
            // Regular variable access
            LPSCRIPT_VARIABLE Variable = ScriptFindVariableInScope(Parser->CurrentScope, VarName, TRUE);
            if (Variable == NULL) {
                *Error = SCRIPT_ERROR_UNDEFINED_VAR;
                return 0.0f;
            }

            if (Variable->Type == SCRIPT_VAR_INTEGER) {
                return (F32)Variable->Value.Integer;
            } else if (Variable->Type == SCRIPT_VAR_FLOAT) {
                return Variable->Value.Float;
            } else {
                *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                return 0.0f;
            }
        }
    }

    if (Parser->CurrentToken.Type == TOKEN_LPAREN) {
        ScriptNextToken(Parser);
        F32 Value = ScriptParseExpression(Parser, Error);
        if (*Error != SCRIPT_OK) return 0.0f;

        if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
            *Error = SCRIPT_ERROR_SYNTAX;
            return 0.0f;
        }

        ScriptNextToken(Parser);
        return Value;
    }

    *Error = SCRIPT_ERROR_SYNTAX;
    return 0.0f;
}

/************************************************************************/

/**
 * @brief Create a new array with initial capacity.
 * @param InitialCapacity Initial capacity of the array
 * @return Pointer to new array or NULL on failure
 */
LPSCRIPT_ARRAY ScriptCreateArray(U32 InitialCapacity) {
    if (InitialCapacity == 0) InitialCapacity = 4;

    LPSCRIPT_ARRAY Array = (LPSCRIPT_ARRAY)HeapAlloc(sizeof(SCRIPT_ARRAY));
    if (Array == NULL) return NULL;

    Array->Elements = (LPVOID*)HeapAlloc(InitialCapacity * sizeof(LPVOID));
    Array->ElementTypes = (SCRIPT_VAR_TYPE*)HeapAlloc(InitialCapacity * sizeof(SCRIPT_VAR_TYPE));

    if (Array->Elements == NULL || Array->ElementTypes == NULL) {
        if (Array->Elements) HeapFree(Array->Elements);
        if (Array->ElementTypes) HeapFree(Array->ElementTypes);
        HeapFree(Array);
        return NULL;
    }

    Array->Size = 0;
    Array->Capacity = InitialCapacity;

    return Array;
}

/************************************************************************/

/**
 * @brief Destroy an array and free all resources.
 * @param Array Array to destroy
 */
void ScriptDestroyArray(LPSCRIPT_ARRAY Array) {
    if (Array == NULL) return;

    // Free all string elements
    for (U32 i = 0; i < Array->Size; i++) {
        if (Array->ElementTypes[i] == SCRIPT_VAR_STRING && Array->Elements[i]) {
            HeapFree(Array->Elements[i]);
        }
    }

    HeapFree(Array->Elements);
    HeapFree(Array->ElementTypes);
    HeapFree(Array);
}

/************************************************************************/

/**
 * @brief Set an array element value.
 * @param Array Array to modify
 * @param Index Element index
 * @param Type Element type
 * @param Value Element value
 * @return Script error code
 */
SCRIPT_ERROR ScriptArraySet(LPSCRIPT_ARRAY Array, U32 Index, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value) {
    if (Array == NULL) return SCRIPT_ERROR_SYNTAX;

    // Resize array if necessary
    if (Index >= Array->Capacity) {
        U32 NewCapacity = Index + 1;
        if (NewCapacity < Array->Capacity * 2) NewCapacity = Array->Capacity * 2;

        LPVOID* NewElements = (LPVOID*)HeapAlloc(NewCapacity * sizeof(LPVOID));
        SCRIPT_VAR_TYPE* NewTypes = (SCRIPT_VAR_TYPE*)HeapAlloc(NewCapacity * sizeof(SCRIPT_VAR_TYPE));

        if (NewElements == NULL || NewTypes == NULL) {
            if (NewElements) HeapFree(NewElements);
            if (NewTypes) HeapFree(NewTypes);
            return SCRIPT_ERROR_OUT_OF_MEMORY;
        }

        // Copy existing elements
        for (U32 i = 0; i < Array->Size; i++) {
            NewElements[i] = Array->Elements[i];
            NewTypes[i] = Array->ElementTypes[i];
        }

        HeapFree(Array->Elements);
        HeapFree(Array->ElementTypes);
        Array->Elements = NewElements;
        Array->ElementTypes = NewTypes;
        Array->Capacity = NewCapacity;
    }

    // Free existing string value if overwriting
    if (Index < Array->Size && Array->ElementTypes[Index] == SCRIPT_VAR_STRING && Array->Elements[Index]) {
        HeapFree(Array->Elements[Index]);
    }

    Array->ElementTypes[Index] = Type;

    // Copy value based on type
    if (Type == SCRIPT_VAR_STRING && Value.String) {
        U32 Len = StringLength(Value.String) + 1;
        Array->Elements[Index] = HeapAlloc(Len);
        if (Array->Elements[Index] == NULL) return SCRIPT_ERROR_OUT_OF_MEMORY;
        StringCopy((LPSTR)Array->Elements[Index], Value.String);
    } else if (Type == SCRIPT_VAR_INTEGER) {
        I32* IntPtr = (I32*)HeapAlloc(sizeof(I32));
        if (IntPtr == NULL) return SCRIPT_ERROR_OUT_OF_MEMORY;
        *IntPtr = Value.Integer;
        Array->Elements[Index] = IntPtr;
    } else if (Type == SCRIPT_VAR_FLOAT) {
        F32* FloatPtr = (F32*)HeapAlloc(sizeof(F32));
        if (FloatPtr == NULL) return SCRIPT_ERROR_OUT_OF_MEMORY;
        *FloatPtr = Value.Float;
        Array->Elements[Index] = FloatPtr;
    } else {
        Array->Elements[Index] = NULL;
    }

    if (Index >= Array->Size) Array->Size = Index + 1;

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Get an array element value.
 * @param Array Array to query
 * @param Index Element index
 * @param Type Pointer to receive element type
 * @param Value Pointer to receive element value
 * @return Script error code
 */
SCRIPT_ERROR ScriptArrayGet(LPSCRIPT_ARRAY Array, U32 Index, SCRIPT_VAR_TYPE* Type, SCRIPT_VAR_VALUE* Value) {
    if (Array == NULL || Type == NULL || Value == NULL) return SCRIPT_ERROR_SYNTAX;
    if (Index >= Array->Size) return SCRIPT_ERROR_UNDEFINED_VAR;

    *Type = Array->ElementTypes[Index];

    if (*Type == SCRIPT_VAR_STRING) {
        Value->String = (LPSTR)Array->Elements[Index];
    } else if (*Type == SCRIPT_VAR_INTEGER) {
        Value->Integer = *(I32*)Array->Elements[Index];
    } else if (*Type == SCRIPT_VAR_FLOAT) {
        Value->Float = *(F32*)Array->Elements[Index];
    } else {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Set an array element in a script variable.
 * @param Context Script context
 * @param Name Variable name
 * @param Index Array index
 * @param Type Element type
 * @param Value Element value
 * @return Pointer to variable or NULL on failure
 */
LPSCRIPT_VARIABLE ScriptSetArrayElement(LPSCRIPT_CONTEXT Context, LPCSTR Name, U32 Index, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value) {
    if (Context == NULL || Name == NULL) return NULL;

    LPSCRIPT_VARIABLE Variable = ScriptGetVariable(Context, Name);

    // Create array variable if it doesn't exist
    if (Variable == NULL) {
        SCRIPT_VAR_VALUE ArrayValue;
        ArrayValue.Array = ScriptCreateArray(0);
        if (ArrayValue.Array == NULL) return NULL;

        Variable = ScriptSetVariable(Context, Name, SCRIPT_VAR_ARRAY, ArrayValue);
        if (Variable == NULL) {
            ScriptDestroyArray(ArrayValue.Array);
            return NULL;
        }
    }

    // Ensure variable is an array
    if (Variable->Type != SCRIPT_VAR_ARRAY) {
        return NULL;
    }

    SCRIPT_ERROR Error = ScriptArraySet(Variable->Value.Array, Index, Type, Value);
    if (Error != SCRIPT_OK) return NULL;

    return Variable;
}

/************************************************************************/

/**
 * @brief Get an array element from a script variable.
 * @param Context Script context
 * @param Name Variable name
 * @param Index Array index
 * @return Pointer to temporary variable containing element value, or NULL on failure
 */
LPSCRIPT_VARIABLE ScriptGetArrayElement(LPSCRIPT_CONTEXT Context, LPCSTR Name, U32 Index) {
    if (Context == NULL || Name == NULL) return NULL;

    LPSCRIPT_VARIABLE Variable = ScriptGetVariable(Context, Name);
    if (Variable == NULL || Variable->Type != SCRIPT_VAR_ARRAY) return NULL;

    SCRIPT_VAR_TYPE ElementType;
    SCRIPT_VAR_VALUE ElementValue;

    SCRIPT_ERROR Error = ScriptArrayGet(Variable->Value.Array, Index, &ElementType, &ElementValue);
    if (Error != SCRIPT_OK) return NULL;

    // Create a temporary variable to hold the element value
    LPSCRIPT_VARIABLE TempVar = (LPSCRIPT_VARIABLE)HeapAlloc(sizeof(SCRIPT_VARIABLE));
    if (TempVar == NULL) return NULL;

    MemorySet(TempVar, 0, sizeof(SCRIPT_VARIABLE));
    TempVar->Type = ElementType;
    TempVar->Value = ElementValue;
    TempVar->RefCount = 1;

    return TempVar;
}

/************************************************************************/

/**
 * @brief Check if a string is a script keyword.
 * @param Str String to check
 * @return TRUE if the string is a keyword
 */
static BOOL ScriptIsKeyword(LPCSTR Str) {
    return (StringCompare(Str, TEXT("if")) == 0 ||
            StringCompare(Str, TEXT("else")) == 0 ||
            StringCompare(Str, TEXT("for")) == 0);
}

/************************************************************************/

/**
 * @brief Parse a statement (assignment, if, for, or block).
 * @param Parser Parser state
 * @return Script error code
 */
static SCRIPT_ERROR ScriptParseStatement(LPSCRIPT_PARSER Parser) {
    if (Parser->CurrentToken.Type == TOKEN_IF) {
        return ScriptParseIfStatement(Parser);
    } else if (Parser->CurrentToken.Type == TOKEN_FOR) {
        return ScriptParseForStatement(Parser);
    } else if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
        return ScriptParseBlock(Parser);
    } else if (Parser->CurrentToken.Type == TOKEN_IDENTIFIER) {
        return ScriptParseAssignment(Parser);
    } else if (Parser->CurrentToken.Type == TOKEN_EOF) {
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_SYNTAX;
}

/************************************************************************/

/**
 * @brief Parse a command block { ... }.
 * @param Parser Parser state
 * @return Script error code
 */
static SCRIPT_ERROR ScriptParseBlock(LPSCRIPT_PARSER Parser) {
    if (Parser->CurrentToken.Type != TOKEN_LBRACE) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // Get context from variables pointer
    LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)((U8*)Parser->Variables - ((U8*)&((LPSCRIPT_CONTEXT)0)->Variables - (U8*)0));

    // Push new scope for this block
    LPSCRIPT_SCOPE OldScope = Parser->CurrentScope;
    LPSCRIPT_SCOPE NewScope = ScriptCreateScope(Parser->CurrentScope);
    if (NewScope == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }
    Parser->CurrentScope = NewScope;
    Context->CurrentScope = NewScope;

    // Parse statements until we hit the closing brace
    SCRIPT_ERROR Error = SCRIPT_OK;
    while (Parser->CurrentToken.Type != TOKEN_RBRACE && Parser->CurrentToken.Type != TOKEN_EOF) {
        Error = ScriptParseStatement(Parser);
        if (Error != SCRIPT_OK) {
            break;
        }

        // Semicolon is mandatory to terminate statement
        if (Parser->CurrentToken.Type != TOKEN_SEMICOLON && Parser->CurrentToken.Type != TOKEN_RBRACE) {
            DEBUG(TEXT("[ScriptParseBlock] Expected semicolon or }, got token type %d"), Parser->CurrentToken.Type);
            Error = SCRIPT_ERROR_SYNTAX;
            break;
        }

        // Skip semicolon
        if (Parser->CurrentToken.Type == TOKEN_SEMICOLON) {
            ScriptNextToken(Parser);
        }
    }

    // Pop scope
    Parser->CurrentScope = OldScope;
    Context->CurrentScope = OldScope;
    ScriptDestroyScope(NewScope);

    if (Error != SCRIPT_OK) {
        return Error;
    }

    if (Parser->CurrentToken.Type != TOKEN_RBRACE) {
        return SCRIPT_ERROR_UNMATCHED_BRACE;
    }
    ScriptNextToken(Parser);

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Parse an if statement.
 * @param Parser Parser state
 * @return Script error code
 */
static SCRIPT_ERROR ScriptParseIfStatement(LPSCRIPT_PARSER Parser) {
    if (Parser->CurrentToken.Type != TOKEN_IF) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // Expect opening parenthesis
    if (Parser->CurrentToken.Type != TOKEN_LPAREN) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // Parse condition
    SCRIPT_ERROR Error = SCRIPT_OK;
    F32 Condition = ScriptParseComparison(Parser, &Error);
    if (Error != SCRIPT_OK) {
        return Error;
    }

    // Expect closing parenthesis
    if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // If condition is true (non-zero), execute the if block
    if (Condition != 0.0f) {
        Error = ScriptParseStatement(Parser);
        if (Error != SCRIPT_OK) {
            return Error;
        }

        // Skip the else block if present
        if (Parser->CurrentToken.Type == TOKEN_ELSE) {
            ScriptNextToken(Parser);
            // Skip the else statement without executing it
            if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
                // Skip entire block
                U32 BraceCount = 1;
                ScriptNextToken(Parser);
                while (BraceCount > 0 && Parser->CurrentToken.Type != TOKEN_EOF) {
                    if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
                        BraceCount++;
                    } else if (Parser->CurrentToken.Type == TOKEN_RBRACE) {
                        BraceCount--;
                    }
                    ScriptNextToken(Parser);
                }
            } else {
                // Skip single statement - just advance to next token
                ScriptNextToken(Parser);
            }
        }
    } else {
        // Skip the if block without executing it
        if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
            // Skip entire block
            U32 BraceCount = 1;
            ScriptNextToken(Parser);
            while (BraceCount > 0 && Parser->CurrentToken.Type != TOKEN_EOF) {
                if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
                    BraceCount++;
                } else if (Parser->CurrentToken.Type == TOKEN_RBRACE) {
                    BraceCount--;
                }
                ScriptNextToken(Parser);
            }
        } else {
            // Skip single statement - just advance to next token
            ScriptNextToken(Parser);
        }

        // Execute else block if present
        if (Parser->CurrentToken.Type == TOKEN_ELSE) {
            ScriptNextToken(Parser);
            Error = ScriptParseStatement(Parser);
            if (Error != SCRIPT_OK) {
                return Error;
            }
        }
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Parse a for statement.
 * @param Parser Parser state
 * @return Script error code
 */
static SCRIPT_ERROR ScriptParseForStatement(LPSCRIPT_PARSER Parser) {
    if (Parser->CurrentToken.Type != TOKEN_FOR) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // Expect opening parenthesis
    if (Parser->CurrentToken.Type != TOKEN_LPAREN) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // Parse initialization (assignment)
    SCRIPT_ERROR Error = ScriptParseAssignment(Parser);
    if (Error != SCRIPT_OK) {
        return Error;
    }

    // Expect semicolon
    if (Parser->CurrentToken.Type != TOKEN_SEMICOLON) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // Remember position for condition check
    U32 ConditionPos = Parser->Position;

    // Parse and evaluate condition
    F32 Condition = ScriptParseComparison(Parser, &Error);
    if (Error != SCRIPT_OK) {
        return Error;
    }

    // Expect semicolon
    if (Parser->CurrentToken.Type != TOKEN_SEMICOLON) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // Remember increment statement position and parse it once to validate
    U32 IncrementPos = Parser->Position;
    Error = ScriptParseAssignment(Parser);
    if (Error != SCRIPT_OK) {
        return Error;
    }

    // Expect closing parenthesis
    if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
        return SCRIPT_ERROR_SYNTAX;
    }
    ScriptNextToken(Parser);

    // Remember body position
    U32 BodyPos = Parser->Position;
    TOKEN_TYPE BodyTokenType = Parser->CurrentToken.Type;
    STR BodyValue[MAX_TOKEN_LENGTH];
    StringCopy(BodyValue, Parser->CurrentToken.Value);

    // Execute loop while condition is true
    U32 LoopCount = 0;
    const U32 MAX_ITERATIONS = 1000; // Safety limit

    while (Condition != 0.0f && LoopCount < MAX_ITERATIONS) {
        // Reset parser to body position and execute body
        Parser->Position = BodyPos;
        Parser->CurrentToken.Type = BodyTokenType;
        StringCopy(Parser->CurrentToken.Value, BodyValue);

        Error = ScriptParseStatement(Parser);
        if (Error != SCRIPT_OK) {
            return Error;
        }

        // Execute increment statement
        Parser->Position = IncrementPos;
        ScriptNextToken(Parser); // Re-tokenize from increment position
        Error = ScriptParseAssignment(Parser);
        if (Error != SCRIPT_OK) {
            return Error;
        }

        // Re-evaluate condition
        Parser->Position = ConditionPos;
        ScriptNextToken(Parser); // Re-tokenize from condition position
        Condition = ScriptParseComparison(Parser, &Error);
        if (Error != SCRIPT_OK) {
            return Error;
        }

        LoopCount++;
    }

    if (LoopCount >= MAX_ITERATIONS) {
        DEBUG(TEXT("[ScriptParseForStatement] Loop exceeded maximum iterations"));
    }

    // Position parser after the for statement
    Parser->Position = BodyPos;
    Parser->CurrentToken.Type = BodyTokenType;
    StringCopy(Parser->CurrentToken.Value, BodyValue);

    // Skip the body to position after the for statement
    if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
        U32 BraceCount = 1;
        ScriptNextToken(Parser);
        while (BraceCount > 0 && Parser->CurrentToken.Type != TOKEN_EOF) {
            if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
                BraceCount++;
            } else if (Parser->CurrentToken.Type == TOKEN_RBRACE) {
                BraceCount--;
            }
            ScriptNextToken(Parser);
        }
    } else {
        ScriptNextToken(Parser);
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Create a new scope with optional parent.
 * @param Parent Parent scope or NULL for root scope
 * @return Pointer to new scope or NULL on failure
 */
LPSCRIPT_SCOPE ScriptCreateScope(LPSCRIPT_SCOPE Parent) {
    LPSCRIPT_SCOPE Scope = (LPSCRIPT_SCOPE)HeapAlloc(sizeof(SCRIPT_SCOPE));
    if (Scope == NULL) {
        DEBUG(TEXT("[ScriptCreateScope] Failed to allocate scope"));
        return NULL;
    }

    MemorySet(Scope, 0, sizeof(SCRIPT_SCOPE));

    // Initialize hash table
    for (U32 i = 0; i < SCRIPT_VAR_HASH_SIZE; i++) {
        Scope->Buckets[i] = NewList(NULL, HeapAlloc, HeapFree);
        if (Scope->Buckets[i] == NULL) {
            DEBUG(TEXT("[ScriptCreateScope] Failed to create bucket %d"), i);
            ScriptDestroyScope(Scope);
            return NULL;
        }
    }

    Scope->Parent = Parent;
    Scope->ScopeLevel = Parent ? Parent->ScopeLevel + 1 : 0;
    Scope->Count = 0;

    return Scope;
}

/************************************************************************/

/**
 * @brief Destroy a scope and all its variables.
 * @param Scope Scope to destroy
 */
void ScriptDestroyScope(LPSCRIPT_SCOPE Scope) {
    if (Scope == NULL) return;

    // Free all variables in this scope
    for (U32 i = 0; i < SCRIPT_VAR_HASH_SIZE; i++) {
        if (Scope->Buckets[i]) {
            LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)Scope->Buckets[i]->First;
            while (Variable) {
                LPSCRIPT_VARIABLE Next = (LPSCRIPT_VARIABLE)Variable->Next;
                ScriptFreeVariable(Variable);
                Variable = Next;
            }
            DeleteList(Scope->Buckets[i]);
        }
    }

    HeapFree(Scope);
}

/************************************************************************/

/**
 * @brief Push a new scope onto the context scope stack.
 * @param Context Script context
 * @return Pointer to new scope or NULL on failure
 */
LPSCRIPT_SCOPE ScriptPushScope(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL) return NULL;

    LPSCRIPT_SCOPE NewScope = ScriptCreateScope(Context->CurrentScope);
    if (NewScope == NULL) {
        return NULL;
    }

    Context->CurrentScope = NewScope;

    return NewScope;
}

/************************************************************************/

/**
 * @brief Pop the current scope and return to parent.
 * @param Context Script context
 */
void ScriptPopScope(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL || Context->CurrentScope == NULL) return;

    LPSCRIPT_SCOPE OldScope = Context->CurrentScope;
    Context->CurrentScope = OldScope->Parent;

    // Don't destroy the global scope
    if (OldScope != Context->GlobalScope) {
        ScriptDestroyScope(OldScope);
    }
}

/************************************************************************/

/**
 * @brief Find a variable in a scope, optionally searching parent scopes.
 * @param Scope Starting scope
 * @param Name Variable name
 * @param SearchParents TRUE to search parent scopes
 * @return Pointer to variable or NULL if not found
 */
LPSCRIPT_VARIABLE ScriptFindVariableInScope(LPSCRIPT_SCOPE Scope, LPCSTR Name, BOOL SearchParents) {
    if (Scope == NULL || Name == NULL) return NULL;

    U32 Hash = ScriptHashVariable(Name);
    LPLIST Bucket = Scope->Buckets[Hash];

    // Search in current scope
    for (LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)Bucket->First; Variable; Variable = (LPSCRIPT_VARIABLE)Variable->Next) {
        if (StringCompare(Variable->Name, Name) == 0) {
            return Variable;
        }
    }

    // Search in parent scopes if requested
    if (SearchParents && Scope->Parent) {
        return ScriptFindVariableInScope(Scope->Parent, Name, TRUE);
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Set a variable in a specific scope.
 * @param Scope Target scope
 * @param Name Variable name
 * @param Type Variable type
 * @param Value Variable value
 * @return Pointer to variable or NULL on failure
 */
LPSCRIPT_VARIABLE ScriptSetVariableInScope(LPSCRIPT_SCOPE Scope, LPCSTR Name, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value) {
    if (Scope == NULL || Name == NULL) return NULL;

    U32 Hash = ScriptHashVariable(Name);
    LPLIST Bucket = Scope->Buckets[Hash];

    // Check if variable already exists in this scope only
    for (LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)Bucket->First; Variable; Variable = (LPSCRIPT_VARIABLE)Variable->Next) {
        if (StringCompare(Variable->Name, Name) == 0) {
            // Update existing variable
            if (Variable->Type == SCRIPT_VAR_STRING && Variable->Value.String) {
                HeapFree(Variable->Value.String);
                Variable->Value.String = NULL;
            }

            Variable->Type = Type;
            Variable->Value = Value;

            // Duplicate string value
            if (Type == SCRIPT_VAR_STRING && Value.String) {
                U32 Len = StringLength(Value.String) + 1;
                Variable->Value.String = (LPSTR)HeapAlloc(Len);
                if (Variable->Value.String) {
                    StringCopy(Variable->Value.String, Value.String);
                }
            }

            return Variable;
        }
    }

    // Create new variable in this scope
    LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)HeapAlloc(sizeof(SCRIPT_VARIABLE));
    if (Variable == NULL) return NULL;

    MemorySet(Variable, 0, sizeof(SCRIPT_VARIABLE));
    StringCopy(Variable->Name, Name);
    Variable->Type = Type;
    Variable->Value = Value;
    Variable->RefCount = 1;

    // Duplicate string value
    if (Type == SCRIPT_VAR_STRING && Value.String) {
        U32 Len = StringLength(Value.String) + 1;
        Variable->Value.String = (LPSTR)HeapAlloc(Len);
        if (Variable->Value.String) {
            StringCopy(Variable->Value.String, Value.String);
        } else {
            HeapFree(Variable);
            return NULL;
        }
    }

    ListAddItem(Bucket, Variable);
    Scope->Count++;

    return Variable;
}
