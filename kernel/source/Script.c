
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

#include "Base.h"
#include "Heap.h"
#include "List.h"
#include "Log.h"
#include "String.h"
#include "Script.h"
#include "User.h"

/************************************************************************/

static U32 ScriptHashVariable(LPCSTR Name);
static void ScriptFreeVariable(LPSCRIPT_VARIABLE Variable);
static void ScriptInitParser(LPSCRIPT_PARSER Parser, LPCSTR Input, LPSCRIPT_VAR_TABLE Variables, LPSCRIPT_CALLBACKS Callbacks, LPSCRIPT_SCOPE CurrentScope);
static void ScriptNextToken(LPSCRIPT_PARSER Parser);
static LPAST_NODE ScriptParseExpressionAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseComparisonAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseTermAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseFactorAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseAssignmentAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseBlockAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseIfStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseForStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static LPAST_NODE ScriptParseShellCommandExpression(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
static BOOL ScriptShouldParseShellCommand(LPSCRIPT_PARSER Parser);
static BOOL ScriptIsKeyword(LPCSTR Str);
static F32 ScriptEvaluateExpression(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error);
static SCRIPT_ERROR ScriptExecuteAssignment(LPSCRIPT_PARSER Parser, LPAST_NODE Node);
static SCRIPT_ERROR ScriptExecuteBlock(LPSCRIPT_PARSER Parser, LPAST_NODE Node);
static BOOL IsInteger(F32 Value);
static void ScriptCalculateLineColumn(LPCSTR Input, U32 Position, U32* Line, U32* Column);

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
 * @brief Execute a script (can contain multiple lines) - Two-pass architecture.
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

    SCRIPT_PARSER Parser;
    ScriptInitParser(&Parser, Script, &Context->Variables, &Context->Callbacks, Context->CurrentScope);

    // PASS 1: Parse script and build AST
    LPAST_NODE Root = ScriptCreateASTNode(AST_BLOCK);
    if (Root == NULL) {
        StringCopy(Context->ErrorMessage, TEXT("Out of memory"));
        Context->ErrorCode = SCRIPT_ERROR_OUT_OF_MEMORY;
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    Root->Data.Block.Capacity = 16;
    Root->Data.Block.Statements = (LPAST_NODE*)HeapAlloc(Root->Data.Block.Capacity * sizeof(LPAST_NODE));
    if (Root->Data.Block.Statements == NULL) {
        ScriptDestroyAST(Root);
        StringCopy(Context->ErrorMessage, TEXT("Out of memory"));
        Context->ErrorCode = SCRIPT_ERROR_OUT_OF_MEMORY;
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }
    Root->Data.Block.Count = 0;

    SCRIPT_ERROR Error = SCRIPT_OK;

    // Parse all statements until EOF
    while (Parser.CurrentToken.Type != TOKEN_EOF) {
        LPAST_NODE Statement = ScriptParseStatementAST(&Parser, &Error);
        if (Error != SCRIPT_OK) {
            StringPrintFormat(Context->ErrorMessage, TEXT("Syntax error (l:%d,c:%d)"), Parser.CurrentToken.Line, Parser.CurrentToken.Column);
            Context->ErrorCode = Error;
            ScriptDestroyAST(Root);
            return Error;
        }

        // Add statement to root block
        if (Root->Data.Block.Count >= Root->Data.Block.Capacity) {
            Root->Data.Block.Capacity *= 2;
            LPAST_NODE* NewStatements = (LPAST_NODE*)HeapAlloc(Root->Data.Block.Capacity * sizeof(LPAST_NODE));
            if (NewStatements == NULL) {
                ScriptDestroyAST(Statement);
                ScriptDestroyAST(Root);
                StringCopy(Context->ErrorMessage, TEXT("Out of memory"));
                Context->ErrorCode = SCRIPT_ERROR_OUT_OF_MEMORY;
                return SCRIPT_ERROR_OUT_OF_MEMORY;
            }
            for (U32 i = 0; i < Root->Data.Block.Count; i++) {
                NewStatements[i] = Root->Data.Block.Statements[i];
            }
            HeapFree(Root->Data.Block.Statements);
            Root->Data.Block.Statements = NewStatements;
        }

        Root->Data.Block.Statements[Root->Data.Block.Count++] = Statement;

        // Semicolon is mandatory after assignments, optional after blocks/if/for
        if (Statement->Type == AST_ASSIGNMENT) {
            if (Parser.CurrentToken.Type != TOKEN_SEMICOLON && Parser.CurrentToken.Type != TOKEN_EOF) {
                DEBUG(TEXT("[ScriptExecute] Expected semicolon after assignment, got token type %d (l:%d,c:%d)"), Parser.CurrentToken.Type, Parser.CurrentToken.Line, Parser.CurrentToken.Column);
                StringPrintFormat(Context->ErrorMessage, TEXT("Expected semicolon (l:%d,c:%d)"), Parser.CurrentToken.Line, Parser.CurrentToken.Column);
                Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                ScriptDestroyAST(Root);
                return SCRIPT_ERROR_SYNTAX;
            }
            if (Parser.CurrentToken.Type == TOKEN_SEMICOLON) {
                ScriptNextToken(&Parser);
            }
        } else {
            // For blocks, if, for: semicolon is optional
            if (Parser.CurrentToken.Type == TOKEN_SEMICOLON) {
                ScriptNextToken(&Parser);
            }
        }
    }

    // PASS 2: Execute AST - Execute statements directly without creating a new scope
    for (U32 i = 0; i < Root->Data.Block.Count; i++) {
        Error = ScriptExecuteAST(&Parser, Root->Data.Block.Statements[i]);
        if (Error != SCRIPT_OK) {
            break;
        }
    }

    ScriptDestroyAST(Root);

    if (Error == SCRIPT_OK && Context->ErrorCode != SCRIPT_OK) {
        Error = Context->ErrorCode;
    }

    if (Error != SCRIPT_OK) {
        if (Context->ErrorMessage[0] == STR_NULL) {
            StringCopy(Context->ErrorMessage, TEXT("Execution error"));
        }
        Context->ErrorCode = Error;
    }

    return Error;
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
        if (STRINGS_EQUAL(Variable->Name, Name)) {
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
 * @brief Create a new AST node.
 * @param Type Node type
 * @return Pointer to new node or NULL on failure
 */
LPAST_NODE ScriptCreateASTNode(AST_NODE_TYPE Type) {
    LPAST_NODE Node = (LPAST_NODE)HeapAlloc(sizeof(AST_NODE));
    if (Node == NULL) {
        DEBUG(TEXT("[ScriptCreateASTNode] Failed to allocate AST node"));
        return NULL;
    }

    MemorySet(Node, 0, sizeof(AST_NODE));
    Node->Type = Type;
    Node->Next = NULL;

    return Node;
}

/************************************************************************/

/**
 * @brief Destroy an AST node and all its children.
 * @param Node Node to destroy
 */
void ScriptDestroyAST(LPAST_NODE Node) {
    if (Node == NULL) return;

    switch (Node->Type) {
        case AST_ASSIGNMENT:
            if (Node->Data.Assignment.Expression) {
                ScriptDestroyAST(Node->Data.Assignment.Expression);
            }
            if (Node->Data.Assignment.ArrayIndexExpr) {
                ScriptDestroyAST(Node->Data.Assignment.ArrayIndexExpr);
            }
            break;

        case AST_IF:
            if (Node->Data.If.Condition) {
                ScriptDestroyAST(Node->Data.If.Condition);
            }
            if (Node->Data.If.Then) {
                ScriptDestroyAST(Node->Data.If.Then);
            }
            if (Node->Data.If.Else) {
                ScriptDestroyAST(Node->Data.If.Else);
            }
            break;

        case AST_FOR:
            if (Node->Data.For.Init) {
                ScriptDestroyAST(Node->Data.For.Init);
            }
            if (Node->Data.For.Condition) {
                ScriptDestroyAST(Node->Data.For.Condition);
            }
            if (Node->Data.For.Increment) {
                ScriptDestroyAST(Node->Data.For.Increment);
            }
            if (Node->Data.For.Body) {
                ScriptDestroyAST(Node->Data.For.Body);
            }
            break;

        case AST_BLOCK:
            if (Node->Data.Block.Statements) {
                for (U32 i = 0; i < Node->Data.Block.Count; i++) {
                    ScriptDestroyAST(Node->Data.Block.Statements[i]);
                }
                HeapFree(Node->Data.Block.Statements);
            }
            break;

        case AST_EXPRESSION:
            if (Node->Data.Expression.ArrayIndexExpr) {
                ScriptDestroyAST(Node->Data.Expression.ArrayIndexExpr);
            }
            if (Node->Data.Expression.Left) {
                ScriptDestroyAST(Node->Data.Expression.Left);
            }
            if (Node->Data.Expression.Right) {
                ScriptDestroyAST(Node->Data.Expression.Right);
            }
            if (Node->Data.Expression.IsShellCommand && Node->Data.Expression.CommandLine) {
                HeapFree(Node->Data.Expression.CommandLine);
            }
            break;

        default:
            break;
    }

    // Destroy next node in chain
    if (Node->Next) {
        ScriptDestroyAST(Node->Next);
    }

    HeapFree(Node);
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
 * @brief Check if a floating point value represents an integer.
 * @param Value The value to check
 * @return TRUE if value has no fractional part
 */
static BOOL IsInteger(F32 Value) {
    return Value == (F32)(I32)Value;
}

/************************************************************************/

/**
 * @brief Calculate line and column number from position in input.
 * @param Input Input string
 * @param Position Position in input string
 * @param Line Pointer to receive line number (1-based)
 * @param Column Pointer to receive column number (1-based)
 */
static void ScriptCalculateLineColumn(LPCSTR Input, U32 Position, U32* Line, U32* Column) {
    U32 CurrentLine = 1;
    U32 CurrentColumn = 1;

    for (U32 i = 0; i < Position && Input[i] != STR_NULL; i++) {
        if (Input[i] == '\n') {
            CurrentLine++;
            CurrentColumn = 1;
        } else {
            CurrentColumn++;
        }
    }

    *Line = CurrentLine;
    *Column = CurrentColumn;
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

    // Skip whitespace including newlines
    while (Input[*Pos] == ' ' || Input[*Pos] == '\t' || Input[*Pos] == '\n' || Input[*Pos] == '\r') (*Pos)++;

    Parser->CurrentToken.Position = *Pos;
    ScriptCalculateLineColumn(Input, *Pos, &Parser->CurrentToken.Line, &Parser->CurrentToken.Column);

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
 * @brief Parse assignment statement and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
static LPAST_NODE ScriptParseAssignmentAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
        DEBUG(TEXT("[ScriptParseAssignmentAST] Expected identifier, got type %d (l:%d,c:%d)"), Parser->CurrentToken.Type, Parser->CurrentToken.Line, Parser->CurrentToken.Column);
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }

    LPAST_NODE Node = ScriptCreateASTNode(AST_ASSIGNMENT);
    if (Node == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    StringCopy(Node->Data.Assignment.VarName, Parser->CurrentToken.Value);
    Node->Data.Assignment.IsArrayAccess = FALSE;
    Node->Data.Assignment.ArrayIndexExpr = NULL;

    ScriptNextToken(Parser);

    // Check for array access
    if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
        Node->Data.Assignment.IsArrayAccess = TRUE;
        ScriptNextToken(Parser);

        // Parse array index expression
        Node->Data.Assignment.ArrayIndexExpr = ScriptParseComparisonAST(Parser, Error);
        if (*Error != SCRIPT_OK || Node->Data.Assignment.ArrayIndexExpr == NULL) {
            ScriptDestroyAST(Node);
            return NULL;
        }

        if (Parser->CurrentToken.Type != TOKEN_RBRACKET) {
            DEBUG(TEXT("[ScriptParseAssignmentAST] Expected ], got type %d (l:%d,c:%d)"), Parser->CurrentToken.Type, Parser->CurrentToken.Line, Parser->CurrentToken.Column);
            *Error = SCRIPT_ERROR_SYNTAX;
            ScriptDestroyAST(Node);
            return NULL;
        }
        ScriptNextToken(Parser);
    }

    if (Parser->CurrentToken.Type != TOKEN_OPERATOR || Parser->CurrentToken.Value[0] != '=') {
        DEBUG(TEXT("[ScriptParseAssignmentAST] Expected =, got type %d value '%s' (l:%d,c:%d)"), Parser->CurrentToken.Type, Parser->CurrentToken.Value, Parser->CurrentToken.Line, Parser->CurrentToken.Column);
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(Node);
        return NULL;
    }

    ScriptNextToken(Parser);

    // Parse expression
    Node->Data.Assignment.Expression = ScriptParseComparisonAST(Parser, Error);
    if (*Error != SCRIPT_OK || Node->Data.Assignment.Expression == NULL) {
        ScriptDestroyAST(Node);
        return NULL;
    }

    return Node;
}

/************************************************************************/

/**
 * @brief Parse comparison operators and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
static LPAST_NODE ScriptParseComparisonAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE Left = ScriptParseExpressionAST(Parser, Error);
    if (*Error != SCRIPT_OK || Left == NULL) return NULL;

    while (Parser->CurrentToken.Type == TOKEN_COMPARISON) {
        // Create comparison node
        LPAST_NODE CompNode = ScriptCreateASTNode(AST_EXPRESSION);
        if (CompNode == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Left);
            return NULL;
        }

        CompNode->Data.Expression.TokenType = TOKEN_COMPARISON;
        StringCopy(CompNode->Data.Expression.Value, Parser->CurrentToken.Value);
        CompNode->Data.Expression.Left = Left;
        ScriptNextToken(Parser);

        LPAST_NODE Right = ScriptParseExpressionAST(Parser, Error);
        if (*Error != SCRIPT_OK || Right == NULL) {
            ScriptDestroyAST(CompNode);
            return NULL;
        }

        CompNode->Data.Expression.Right = Right;
        Left = CompNode;
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Parse expression (addition/subtraction) and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
static LPAST_NODE ScriptParseExpressionAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE Left = ScriptParseTermAST(Parser, Error);
    if (*Error != SCRIPT_OK || Left == NULL) return NULL;

    while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
           (Parser->CurrentToken.Value[0] == '+' || Parser->CurrentToken.Value[0] == '-')) {

        LPAST_NODE OpNode = ScriptCreateASTNode(AST_EXPRESSION);
        if (OpNode == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Left);
            return NULL;
        }

        OpNode->Data.Expression.TokenType = TOKEN_OPERATOR;
        OpNode->Data.Expression.Value[0] = Parser->CurrentToken.Value[0];
        OpNode->Data.Expression.Value[1] = STR_NULL;
        OpNode->Data.Expression.Left = Left;
        ScriptNextToken(Parser);

        LPAST_NODE Right = ScriptParseTermAST(Parser, Error);
        if (*Error != SCRIPT_OK || Right == NULL) {
            ScriptDestroyAST(OpNode);
            return NULL;
        }

        OpNode->Data.Expression.Right = Right;
        Left = OpNode;
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Parse term (multiplication/division) and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
static LPAST_NODE ScriptParseTermAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE Left = ScriptParseFactorAST(Parser, Error);
    if (*Error != SCRIPT_OK || Left == NULL) return NULL;

    while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
           (Parser->CurrentToken.Value[0] == '*' || Parser->CurrentToken.Value[0] == '/')) {

        LPAST_NODE OpNode = ScriptCreateASTNode(AST_EXPRESSION);
        if (OpNode == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Left);
            return NULL;
        }

        OpNode->Data.Expression.TokenType = TOKEN_OPERATOR;
        OpNode->Data.Expression.Value[0] = Parser->CurrentToken.Value[0];
        OpNode->Data.Expression.Value[1] = STR_NULL;
        OpNode->Data.Expression.Left = Left;
        ScriptNextToken(Parser);

        LPAST_NODE Right = ScriptParseFactorAST(Parser, Error);
        if (*Error != SCRIPT_OK || Right == NULL) {
            ScriptDestroyAST(OpNode);
            return NULL;
        }

        OpNode->Data.Expression.Right = Right;
        Left = OpNode;
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Parse factor (numbers, variables, parentheses) and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
static LPAST_NODE ScriptParseFactorAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    // NUMBER
    if (Parser->CurrentToken.Type == TOKEN_NUMBER) {
        LPAST_NODE Node = ScriptCreateASTNode(AST_EXPRESSION);
        if (Node == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        Node->Data.Expression.TokenType = TOKEN_NUMBER;
        Node->Data.Expression.NumValue = Parser->CurrentToken.NumValue;
        StringCopy(Node->Data.Expression.Value, Parser->CurrentToken.Value);
        ScriptNextToken(Parser);
        return Node;
    }

    // IDENTIFIER (variable, function call, or array access)
    if (Parser->CurrentToken.Type == TOKEN_IDENTIFIER) {
        LPAST_NODE Node = ScriptCreateASTNode(AST_EXPRESSION);
        if (Node == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        Node->Data.Expression.TokenType = TOKEN_IDENTIFIER;
        StringCopy(Node->Data.Expression.Value, Parser->CurrentToken.Value);
        Node->Data.Expression.IsVariable = TRUE;
        Node->Data.Expression.IsArrayAccess = FALSE;
        Node->Data.Expression.IsFunctionCall = FALSE;

        ScriptNextToken(Parser);

        // Check for function call
        if (Parser->CurrentToken.Type == TOKEN_LPAREN) {
            Node->Data.Expression.IsFunctionCall = TRUE;
            Node->Data.Expression.Left = NULL;
            ScriptNextToken(Parser);

            // Parse argument as expression (allows nested function calls, variables, numbers, strings)
            if (Parser->CurrentToken.Type == TOKEN_RPAREN) {
                // No argument - empty parentheses
                ScriptNextToken(Parser);
            } else {
                // Parse argument expression - store in Left field
                Node->Data.Expression.Left = ScriptParseComparisonAST(Parser, Error);
                if (*Error != SCRIPT_OK || Node->Data.Expression.Left == NULL) {
                    ScriptDestroyAST(Node);
                    return NULL;
                }

                if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
                    DEBUG(TEXT("[ScriptParseFactorAST] Expected RPAREN, got type %d (l:%d,c:%d)"), Parser->CurrentToken.Type, Parser->CurrentToken.Line, Parser->CurrentToken.Column);
                    *Error = SCRIPT_ERROR_SYNTAX;
                    ScriptDestroyAST(Node);
                    return NULL;
                }
                ScriptNextToken(Parser);
            }
        }
        // Check for array access
        else if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
            Node->Data.Expression.IsArrayAccess = TRUE;
            ScriptNextToken(Parser);

            // Parse array index expression
            Node->Data.Expression.ArrayIndexExpr = ScriptParseComparisonAST(Parser, Error);
            if (*Error != SCRIPT_OK || Node->Data.Expression.ArrayIndexExpr == NULL) {
                ScriptDestroyAST(Node);
                return NULL;
            }

            if (Parser->CurrentToken.Type != TOKEN_RBRACKET) {
                *Error = SCRIPT_ERROR_SYNTAX;
                ScriptDestroyAST(Node);
                return NULL;
            }
            ScriptNextToken(Parser);
        }

        return Node;
    }

    // STRING
    if (Parser->CurrentToken.Type == TOKEN_STRING) {
        LPAST_NODE Node = ScriptCreateASTNode(AST_EXPRESSION);
        if (Node == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        Node->Data.Expression.TokenType = TOKEN_STRING;
        StringCopy(Node->Data.Expression.Value, Parser->CurrentToken.Value);
        ScriptNextToken(Parser);
        return Node;
    }

    // PARENTHESES
    if (Parser->CurrentToken.Type == TOKEN_LPAREN) {
        ScriptNextToken(Parser);
        LPAST_NODE Expr = ScriptParseExpressionAST(Parser, Error);
        if (*Error != SCRIPT_OK || Expr == NULL) return NULL;

        if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
            *Error = SCRIPT_ERROR_SYNTAX;
            ScriptDestroyAST(Expr);
            return NULL;
        }

        ScriptNextToken(Parser);
        return Expr;
    }

    *Error = SCRIPT_ERROR_SYNTAX;
    return NULL;
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
 * @brief Evaluate an expression AST node and return its value.
 * @param Parser Parser state (for variable/callback access)
 * @param Expr Expression node
 * @param Error Pointer to error code
 * @return Expression value
 */
static F32 ScriptEvaluateExpression(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
    if (Expr == NULL) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return 0.0f;
    }

    if (Expr->Type != AST_EXPRESSION) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return 0.0f;
    }

    // NUMBER
    if (Expr->Data.Expression.TokenType == TOKEN_NUMBER) {
        return Expr->Data.Expression.NumValue;
    }

    // IDENTIFIER (variable, function call, or array access)
    if (Expr->Data.Expression.TokenType == TOKEN_IDENTIFIER) {
        // Function call
        if (Expr->Data.Expression.IsFunctionCall) {
            if (Expr->Data.Expression.IsShellCommand) {
                if (Parser->Callbacks && Parser->Callbacks->ExecuteCommand) {
                    U32 Result = Parser->Callbacks->ExecuteCommand(
                        Expr->Data.Expression.CommandLine ? Expr->Data.Expression.CommandLine : Expr->Data.Expression.Value,
                        Parser->Callbacks->UserData);

                    if (Result == DF_ERROR_SUCCESS) {
                        return (F32)Result;
                    }

                    LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)((U8*)Parser->Variables -
                        ((U8*)&((LPSCRIPT_CONTEXT)0)->Variables - (U8*)0));

                    if (Context) {
                        Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                        if (Context->ErrorMessage[0] == STR_NULL) {
                            StringPrintFormat(
                                Context->ErrorMessage,
                                TEXT("Command failed (0x%08X)"),
                                Result);
                        }
                    }

                    *Error = SCRIPT_ERROR_SYNTAX;
                    return 0.0f;
                }

                LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)((U8*)Parser->Variables -
                    ((U8*)&((LPSCRIPT_CONTEXT)0)->Variables - (U8*)0));

                if (Context) {
                    Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                    if (Context->ErrorMessage[0] == STR_NULL) {
                        StringCopy(Context->ErrorMessage, TEXT("No command callback registered"));
                    }
                }

                *Error = SCRIPT_ERROR_SYNTAX;
                return 0.0f;
            }

            if (Parser->Callbacks && Parser->Callbacks->CallFunction) {
                LPCSTR ArgString = TEXT("");
                STR ArgBuffer[MAX_TOKEN_LENGTH];

                // Evaluate argument if present
                if (Expr->Data.Expression.Left) {
                    // Check if argument is a string literal
                    if (Expr->Data.Expression.Left->Data.Expression.TokenType == TOKEN_STRING) {
                        ArgString = Expr->Data.Expression.Left->Data.Expression.Value;
                    } else {
                        // Evaluate as expression and convert to string
                        F32 ArgValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Left, Error);
                        if (*Error != SCRIPT_OK) return 0.0f;

                        if (IsInteger(ArgValue)) {
                            StringPrintFormat(ArgBuffer, TEXT("%d"), (I32)ArgValue);
                        } else {
                            StringPrintFormat(ArgBuffer, TEXT("%f"), ArgValue);
                        }
                        ArgString = ArgBuffer;
                    }
                }

                U32 Result = Parser->Callbacks->CallFunction(
                    Expr->Data.Expression.Value,
                    ArgString,
                    Parser->Callbacks->UserData
                );
                return (F32)Result;
            } else {
                *Error = SCRIPT_ERROR_SYNTAX;
                return 0.0f;
            }
        }

        // Array access
        if (Expr->Data.Expression.IsArrayAccess) {
            // Evaluate array index expression
            F32 IndexValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.ArrayIndexExpr, Error);
            if (*Error != SCRIPT_OK) return 0.0f;

            U32 ArrayIndex = (U32)IndexValue;

            // Get context
            LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)((U8*)Parser->Variables - ((U8*)&((LPSCRIPT_CONTEXT)0)->Variables - (U8*)0));
            LPSCRIPT_VARIABLE ElementVar = ScriptGetArrayElement(Context, Expr->Data.Expression.Value, ArrayIndex);

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

            HeapFree(ElementVar);
            return Result;
        }

        // Regular variable
        LPSCRIPT_VARIABLE Variable = ScriptFindVariableInScope(Parser->CurrentScope, Expr->Data.Expression.Value, TRUE);
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

    // OPERATOR or COMPARISON
    if (Expr->Data.Expression.TokenType == TOKEN_OPERATOR || Expr->Data.Expression.TokenType == TOKEN_COMPARISON) {
        F32 Left = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Left, Error);
        if (*Error != SCRIPT_OK) return 0.0f;

        F32 Right = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Right, Error);
        if (*Error != SCRIPT_OK) return 0.0f;

        STR Op = Expr->Data.Expression.Value[0];

        // Arithmetic operators
        if (Op == '+') return Left + Right;
        if (Op == '-') return Left - Right;
        if (Op == '*') return Left * Right;
        if (Op == '/') {
            if (Right == 0.0f) {
                *Error = SCRIPT_ERROR_DIVISION_BY_ZERO;
                return 0.0f;
            }
            // Integer division if both operands are integers (no fractional part)
            if (IsInteger(Left) && IsInteger(Right)) {
                return (F32)((I32)Left / (I32)Right);
            }
            return Left / Right;
        }

        // Comparison operators
        if (StringCompare(Expr->Data.Expression.Value, TEXT("<")) == 0) {
            return (Left < Right) ? 1.0f : 0.0f;
        }
        if (StringCompare(Expr->Data.Expression.Value, TEXT("<=")) == 0) {
            return (Left <= Right) ? 1.0f : 0.0f;
        }
        if (StringCompare(Expr->Data.Expression.Value, TEXT(">")) == 0) {
            return (Left > Right) ? 1.0f : 0.0f;
        }
        if (StringCompare(Expr->Data.Expression.Value, TEXT(">=")) == 0) {
            return (Left >= Right) ? 1.0f : 0.0f;
        }
        if (StringCompare(Expr->Data.Expression.Value, TEXT("==")) == 0) {
            return (Left == Right) ? 1.0f : 0.0f;
        }
        if (StringCompare(Expr->Data.Expression.Value, TEXT("!=")) == 0) {
            return (Left != Right) ? 1.0f : 0.0f;
        }
    }

    *Error = SCRIPT_ERROR_SYNTAX;
    return 0.0f;
}

/************************************************************************/

/**
 * @brief Execute an assignment AST node.
 * @param Parser Parser state
 * @param Node Assignment node
 * @return Script error code
 */
static SCRIPT_ERROR ScriptExecuteAssignment(LPSCRIPT_PARSER Parser, LPAST_NODE Node) {
    if (Node == NULL || Node->Type != AST_ASSIGNMENT) {
        return SCRIPT_ERROR_SYNTAX;
    }

    // Evaluate expression
    SCRIPT_ERROR Error = SCRIPT_OK;
    F32 Value = ScriptEvaluateExpression(Parser, Node->Data.Assignment.Expression, &Error);
    if (Error != SCRIPT_OK) return Error;

    SCRIPT_VAR_VALUE VarValue;
    SCRIPT_VAR_TYPE VarType;

    // Check if value is a pure integer (no fractional part)
    if (IsInteger(Value)) {
        VarValue.Integer = (I32)Value;
        VarType = SCRIPT_VAR_INTEGER;
    } else {
        VarValue.Float = Value;
        VarType = SCRIPT_VAR_FLOAT;
    }

    // Get context
    LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)((U8*)Parser->Variables - ((U8*)&((LPSCRIPT_CONTEXT)0)->Variables - (U8*)0));

    if (Node->Data.Assignment.IsArrayAccess) {
        // Evaluate array index
        F32 IndexValue = ScriptEvaluateExpression(Parser, Node->Data.Assignment.ArrayIndexExpr, &Error);
        if (Error != SCRIPT_OK) return Error;

        U32 ArrayIndex = (U32)IndexValue;

        // Set array element
        if (ScriptSetArrayElement(Context, Node->Data.Assignment.VarName, ArrayIndex, VarType, VarValue) == NULL) {
            return SCRIPT_ERROR_SYNTAX;
        }
    } else {
        // Set regular variable in current scope
        if (ScriptSetVariableInScope(Parser->CurrentScope, Node->Data.Assignment.VarName, VarType, VarValue) == NULL) {
            return SCRIPT_ERROR_SYNTAX;
        }
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Execute a block AST node.
 * @param Parser Parser state
 * @param Node Block node
 * @return Script error code
 */
static SCRIPT_ERROR ScriptExecuteBlock(LPSCRIPT_PARSER Parser, LPAST_NODE Node) {
    if (Node == NULL || Node->Type != AST_BLOCK) {
        return SCRIPT_ERROR_SYNTAX;
    }

    // Execute all statements in the block without creating a new scope
    // This allows variables created in loops/if bodies to persist
    SCRIPT_ERROR Error = SCRIPT_OK;
    for (U32 i = 0; i < Node->Data.Block.Count; i++) {
        Error = ScriptExecuteAST(Parser, Node->Data.Block.Statements[i]);
        if (Error != SCRIPT_OK) {
            break;
        }
    }

    return Error;
}

/************************************************************************/

/**
 * @brief Execute an AST node.
 * @param Parser Parser state
 * @param Node AST node to execute
 * @return Script error code
 */
SCRIPT_ERROR ScriptExecuteAST(LPSCRIPT_PARSER Parser, LPAST_NODE Node) {
    if (Node == NULL) {
        return SCRIPT_OK;
    }

    switch (Node->Type) {
        case AST_ASSIGNMENT:
            return ScriptExecuteAssignment(Parser, Node);

        case AST_BLOCK:
            return ScriptExecuteBlock(Parser, Node);

        case AST_IF: {
            // Evaluate condition
            SCRIPT_ERROR Error = SCRIPT_OK;
            F32 Condition = ScriptEvaluateExpression(Parser, Node->Data.If.Condition, &Error);
            if (Error != SCRIPT_OK) return Error;

            // Execute then or else branch
            if (Condition != 0.0f) {
                return ScriptExecuteAST(Parser, Node->Data.If.Then);
            } else if (Node->Data.If.Else != NULL) {
                return ScriptExecuteAST(Parser, Node->Data.If.Else);
            }

            return SCRIPT_OK;
        }

        case AST_FOR: {
            // Execute initialization
            SCRIPT_ERROR Error = ScriptExecuteAST(Parser, Node->Data.For.Init);
            if (Error != SCRIPT_OK) return Error;

            // Execute loop
            U32 LoopCount = 0;
            const U32 MAX_ITERATIONS = 1000; // Safety limit

            while (LoopCount < MAX_ITERATIONS) {
                // Evaluate condition
                F32 Condition = ScriptEvaluateExpression(Parser, Node->Data.For.Condition, &Error);
                if (Error != SCRIPT_OK) return Error;

                if (Condition == 0.0f) break;

                // Execute body
                Error = ScriptExecuteAST(Parser, Node->Data.For.Body);
                if (Error != SCRIPT_OK) return Error;

                // Execute increment
                Error = ScriptExecuteAST(Parser, Node->Data.For.Increment);
                if (Error != SCRIPT_OK) return Error;

                LoopCount++;
            }

            if (LoopCount >= MAX_ITERATIONS) {
                DEBUG(TEXT("[ScriptExecuteAST] Loop exceeded maximum iterations"));
            }

            return SCRIPT_OK;
        }

        case AST_EXPRESSION: {
            // Standalone expression - evaluate it (for function calls)
            SCRIPT_ERROR Error = SCRIPT_OK;
            ScriptEvaluateExpression(Parser, Node, &Error);
            return Error;
        }

        default:
            return SCRIPT_ERROR_SYNTAX;
    }
}

/************************************************************************/

/**
 * @brief Parse a statement (assignment, if, for, or block) and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
static LPAST_NODE ScriptParseStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type == TOKEN_IF) {
        return ScriptParseIfStatementAST(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_FOR) {
        return ScriptParseForStatementAST(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
        return ScriptParseBlockAST(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_STRING) {
        return ScriptParseShellCommandExpression(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_IDENTIFIER) {
        // Could be assignment, expression statement (function call) or shell command
        U32 SavedPosition = Parser->Position;
        SCRIPT_TOKEN SavedToken = Parser->CurrentToken;

        ScriptNextToken(Parser);

        // Check if it's an assignment (= or [)
        if (Parser->CurrentToken.Type == TOKEN_OPERATOR && Parser->CurrentToken.Value[0] == '=') {
            Parser->Position = SavedPosition;
            Parser->CurrentToken = SavedToken;
            return ScriptParseAssignmentAST(Parser, Error);
        } else if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
            Parser->Position = SavedPosition;
            Parser->CurrentToken = SavedToken;
            return ScriptParseAssignmentAST(Parser, Error);
        } else if (Parser->CurrentToken.Type == TOKEN_LPAREN) {
            Parser->Position = SavedPosition;
            Parser->CurrentToken = SavedToken;
            return ScriptParseComparisonAST(Parser, Error);
        }

        Parser->Position = SavedPosition;
        Parser->CurrentToken = SavedToken;

        if (ScriptShouldParseShellCommand(Parser)) {
            return ScriptParseShellCommandExpression(Parser, Error);
        }

        return ScriptParseComparisonAST(Parser, Error);
    }

    *Error = SCRIPT_ERROR_SYNTAX;
    return NULL;
}

/************************************************************************/

/**
 * @brief Determine if the current token sequence should be parsed as a shell command.
 * @param Parser Parser state
 * @return TRUE if the statement should be handled as a shell command
 */
static BOOL ScriptShouldParseShellCommand(LPSCRIPT_PARSER Parser) {
    if (Parser == NULL) return FALSE;

    if (Parser->CurrentToken.Type == TOKEN_STRING) {
        return TRUE;
    }

    if (Parser->CurrentToken.Type == TOKEN_IDENTIFIER) {
        LPCSTR Input = Parser->Input;
        U32 Pos = Parser->Position;

        while (Input[Pos] == ' ' || Input[Pos] == '\t') {
            Pos++;
        }

        if (Input[Pos] == '(') {
            return FALSE;
        }

        return TRUE;
    }

    if (Parser->CurrentToken.Type == TOKEN_OPERATOR) {
        STR FirstChar = Parser->CurrentToken.Value[0];
        if (FirstChar == PATH_SEP || FirstChar == STR_DOT) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Parse a shell command expression and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
static LPAST_NODE ScriptParseShellCommandExpression(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser == NULL || Error == NULL) {
        return NULL;
    }

    LPCSTR Input = Parser->Input;
    U32 Start = Parser->CurrentToken.Position;
    U32 Scan = Start;
    BOOL InQuotes = FALSE;
    STR QuoteChar = STR_NULL;

    while (Input[Scan] != STR_NULL) {
        STR Ch = Input[Scan];

        if (!InQuotes && (Ch == ';' || Ch == '\n' || Ch == '\r')) {
            break;
        }

        if (Ch == '"' || Ch == '\'') {
            if (InQuotes && Ch == QuoteChar) {
                InQuotes = FALSE;
                QuoteChar = STR_NULL;
            } else if (!InQuotes) {
                InQuotes = TRUE;
                QuoteChar = Ch;
            }
        }

        Scan++;
    }

    U32 End = Scan;

    while (End > Start && (Input[End - 1] == ' ' || Input[End - 1] == '\t')) {
        End--;
    }

    if (End <= Start) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }

    LPAST_NODE Node = ScriptCreateASTNode(AST_EXPRESSION);
    if (Node == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    U32 Length = End - Start;
    Node->Data.Expression.CommandLine = (LPSTR)HeapAlloc(Length + 1);
    if (Node->Data.Expression.CommandLine == NULL) {
        ScriptDestroyAST(Node);
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    MemoryCopy(Node->Data.Expression.CommandLine, &Input[Start], Length);
    Node->Data.Expression.CommandLine[Length] = STR_NULL;

    Node->Data.Expression.TokenType = TOKEN_IDENTIFIER;
    Node->Data.Expression.IsVariable = FALSE;
    Node->Data.Expression.IsFunctionCall = TRUE;
    Node->Data.Expression.IsShellCommand = TRUE;

    // Extract command name for Value field (trim whitespace and quotes)
    U32 CmdIndex = 0;
    while (Node->Data.Expression.CommandLine[CmdIndex] == ' ' ||
           Node->Data.Expression.CommandLine[CmdIndex] == '\t') {
        CmdIndex++;
    }

    STR Quote = Node->Data.Expression.CommandLine[CmdIndex];
    BOOL Quoted = (Quote == '"' || Quote == '\'');
    if (Quoted) {
        CmdIndex++;
    }

    U32 NameStart = CmdIndex;
    while (Node->Data.Expression.CommandLine[CmdIndex] != STR_NULL) {
        STR Current = Node->Data.Expression.CommandLine[CmdIndex];
        if (Quoted) {
            if (Current == Quote) {
                break;
            }
        } else if (Current == ' ' || Current == '\t') {
            break;
        }
        CmdIndex++;
    }

    U32 NameLength = CmdIndex - NameStart;
    if (NameLength == 0) {
        ScriptDestroyAST(Node);
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }

    if (NameLength >= MAX_TOKEN_LENGTH) {
        NameLength = MAX_TOKEN_LENGTH - 1;
    }

    MemoryCopy(Node->Data.Expression.Value, &Node->Data.Expression.CommandLine[NameStart], NameLength);
    Node->Data.Expression.Value[NameLength] = STR_NULL;

    Parser->Position = Scan;
    ScriptNextToken(Parser);

    *Error = SCRIPT_OK;
    return Node;
}

/************************************************************************/

/**
 * @brief Parse a command block { ... } and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
static LPAST_NODE ScriptParseBlockAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type != TOKEN_LBRACE) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    LPAST_NODE BlockNode = ScriptCreateASTNode(AST_BLOCK);
    if (BlockNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    BlockNode->Data.Block.Capacity = 16;
    BlockNode->Data.Block.Count = 0;
    BlockNode->Data.Block.Statements = (LPAST_NODE*)HeapAlloc(BlockNode->Data.Block.Capacity * sizeof(LPAST_NODE));
    if (BlockNode->Data.Block.Statements == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        ScriptDestroyAST(BlockNode);
        return NULL;
    }

    // Parse statements until we hit the closing brace
    while (Parser->CurrentToken.Type != TOKEN_RBRACE && Parser->CurrentToken.Type != TOKEN_EOF) {
        LPAST_NODE Statement = ScriptParseStatementAST(Parser, Error);
        if (*Error != SCRIPT_OK || Statement == NULL) {
            ScriptDestroyAST(BlockNode);
            return NULL;
        }

        // Add statement to block
        if (BlockNode->Data.Block.Count >= BlockNode->Data.Block.Capacity) {
            BlockNode->Data.Block.Capacity *= 2;
            LPAST_NODE* NewStatements = (LPAST_NODE*)HeapAlloc(BlockNode->Data.Block.Capacity * sizeof(LPAST_NODE));
            if (NewStatements == NULL) {
                *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                ScriptDestroyAST(Statement);
                ScriptDestroyAST(BlockNode);
                return NULL;
            }
            for (U32 i = 0; i < BlockNode->Data.Block.Count; i++) {
                NewStatements[i] = BlockNode->Data.Block.Statements[i];
            }
            HeapFree(BlockNode->Data.Block.Statements);
            BlockNode->Data.Block.Statements = NewStatements;
        }

        BlockNode->Data.Block.Statements[BlockNode->Data.Block.Count++] = Statement;

        // Semicolon is mandatory after assignments, optional after blocks/if/for
        if (Statement->Type == AST_ASSIGNMENT) {
            if (Parser->CurrentToken.Type != TOKEN_SEMICOLON && Parser->CurrentToken.Type != TOKEN_RBRACE) {
                DEBUG(TEXT("[ScriptParseBlockAST] Expected semicolon or }, got token type %d (l:%d,c:%d)"), Parser->CurrentToken.Type, Parser->CurrentToken.Line, Parser->CurrentToken.Column);
                *Error = SCRIPT_ERROR_SYNTAX;
                ScriptDestroyAST(BlockNode);
                return NULL;
            }
            if (Parser->CurrentToken.Type == TOKEN_SEMICOLON) {
                ScriptNextToken(Parser);
            }
        } else {
            // For blocks, if, for: semicolon is optional
            if (Parser->CurrentToken.Type == TOKEN_SEMICOLON) {
                ScriptNextToken(Parser);
            }
        }
    }

    if (Parser->CurrentToken.Type != TOKEN_RBRACE) {
        *Error = SCRIPT_ERROR_UNMATCHED_BRACE;
        ScriptDestroyAST(BlockNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    return BlockNode;
}

/************************************************************************/

/**
 * @brief Parse an if statement and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
static LPAST_NODE ScriptParseIfStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type != TOKEN_IF) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    // Expect opening parenthesis
    if (Parser->CurrentToken.Type != TOKEN_LPAREN) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    // Create IF node
    LPAST_NODE IfNode = ScriptCreateASTNode(AST_IF);
    if (IfNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    // Parse condition
    IfNode->Data.If.Condition = ScriptParseComparisonAST(Parser, Error);
    if (*Error != SCRIPT_OK || IfNode->Data.If.Condition == NULL) {
        ScriptDestroyAST(IfNode);
        return NULL;
    }

    // Expect closing parenthesis
    if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(IfNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    // Parse then branch
    IfNode->Data.If.Then = ScriptParseStatementAST(Parser, Error);
    if (*Error != SCRIPT_OK || IfNode->Data.If.Then == NULL) {
        ScriptDestroyAST(IfNode);
        return NULL;
    }

    // Parse else branch if present
    IfNode->Data.If.Else = NULL;
    if (Parser->CurrentToken.Type == TOKEN_ELSE) {
        ScriptNextToken(Parser);
        IfNode->Data.If.Else = ScriptParseStatementAST(Parser, Error);
        if (*Error != SCRIPT_OK || IfNode->Data.If.Else == NULL) {
            ScriptDestroyAST(IfNode);
            return NULL;
        }
    }

    return IfNode;
}

/************************************************************************/

/**
 * @brief Parse a for statement and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
static LPAST_NODE ScriptParseForStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type != TOKEN_FOR) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    // Expect opening parenthesis
    if (Parser->CurrentToken.Type != TOKEN_LPAREN) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    // Create FOR node
    LPAST_NODE ForNode = ScriptCreateASTNode(AST_FOR);
    if (ForNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    // Parse initialization (assignment)
    ForNode->Data.For.Init = ScriptParseAssignmentAST(Parser, Error);
    if (*Error != SCRIPT_OK || ForNode->Data.For.Init == NULL) {
        ScriptDestroyAST(ForNode);
        return NULL;
    }

    // Expect semicolon
    if (Parser->CurrentToken.Type != TOKEN_SEMICOLON) {
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(ForNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    // Parse condition
    ForNode->Data.For.Condition = ScriptParseComparisonAST(Parser, Error);
    if (*Error != SCRIPT_OK || ForNode->Data.For.Condition == NULL) {
        ScriptDestroyAST(ForNode);
        return NULL;
    }

    // Expect semicolon
    if (Parser->CurrentToken.Type != TOKEN_SEMICOLON) {
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(ForNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    // Parse increment
    ForNode->Data.For.Increment = ScriptParseAssignmentAST(Parser, Error);
    if (*Error != SCRIPT_OK || ForNode->Data.For.Increment == NULL) {
        ScriptDestroyAST(ForNode);
        return NULL;
    }

    // Expect closing parenthesis
    if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(ForNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    // Parse body
    ForNode->Data.For.Body = ScriptParseStatementAST(Parser, Error);
    if (*Error != SCRIPT_OK || ForNode->Data.For.Body == NULL) {
        ScriptDestroyAST(ForNode);
        return NULL;
    }

    return ForNode;
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
        if (STRINGS_EQUAL(Variable->Name, Name)) {
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

    // First check if variable exists in current or parent scopes
    LPSCRIPT_VARIABLE ExistingVar = ScriptFindVariableInScope(Scope, Name, TRUE);

    SAFE_USE(ExistingVar) {
        // Update existing variable in whichever scope it was found
        if (ExistingVar->Type == SCRIPT_VAR_STRING && ExistingVar->Value.String) {
            HeapFree(ExistingVar->Value.String);
            ExistingVar->Value.String = NULL;
        }

        ExistingVar->Type = Type;
        ExistingVar->Value = Value;

        // Duplicate string value
        if (Type == SCRIPT_VAR_STRING && Value.String) {
            U32 Len = StringLength(Value.String) + 1;
            ExistingVar->Value.String = (LPSTR)HeapAlloc(Len);
            if (ExistingVar->Value.String) {
                StringCopy(ExistingVar->Value.String, Value.String);
            }
        }

        return ExistingVar;
    }

    // Variable doesn't exist anywhere, create new variable in current scope
    U32 Hash = ScriptHashVariable(Name);
    LPLIST Bucket = Scope->Buckets[Hash];

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

