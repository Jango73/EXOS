
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
#include "CoreString.h"
#include "Script.h"
#include "User.h"

/************************************************************************/

static U32 ScriptHashVariable(LPCSTR Name);
static void ScriptFreeVariable(LPSCRIPT_VARIABLE Variable);
static void ScriptInitParser(LPSCRIPT_PARSER Parser, LPCSTR Input, LPSCRIPT_CONTEXT Context);
static void ScriptNextToken(LPSCRIPT_PARSER Parser);
static void ScriptParseStringToken(LPSCRIPT_PARSER Parser, LPCSTR Input, U32* Pos, STR QuoteChar);
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
static void ScriptValueInit(SCRIPT_VALUE* Value);
static void ScriptValueRelease(SCRIPT_VALUE* Value);
static SCRIPT_VALUE ScriptEvaluateExpression(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error);
static SCRIPT_VALUE ScriptEvaluateHostProperty(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error);
static SCRIPT_VALUE ScriptEvaluateArrayAccess(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error);
static SCRIPT_ERROR ScriptPrepareHostValue(SCRIPT_VALUE* Value, const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor, LPVOID DefaultContext);
static BOOL ScriptValueToFloat(const SCRIPT_VALUE* Value, F32* OutValue);
static SCRIPT_ERROR ScriptExecuteAssignment(LPSCRIPT_PARSER Parser, LPAST_NODE Node);
static SCRIPT_ERROR ScriptExecuteBlock(LPSCRIPT_PARSER Parser, LPAST_NODE Node);
static BOOL IsInteger(F32 Value);
static void ScriptCalculateLineColumn(LPCSTR Input, U32 Position, U32* Line, U32* Column);
static U32 ScriptHashHostSymbol(LPCSTR Name);
static BOOL ScriptInitHostRegistry(LPSCRIPT_HOST_REGISTRY Registry);
static void ScriptClearHostRegistryInternal(LPSCRIPT_HOST_REGISTRY Registry);
static LPSCRIPT_HOST_SYMBOL ScriptFindHostSymbol(LPSCRIPT_HOST_REGISTRY Registry, LPCSTR Name);

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

    if (!ScriptInitHostRegistry(&Context->HostRegistry)) {
        DEBUG(TEXT("[ScriptCreateContext] Failed to initialize host registry"));
        ScriptDestroyContext(Context);
        return NULL;
    }

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

    ScriptClearHostRegistryInternal(&Context->HostRegistry);

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
    ScriptInitParser(&Parser, Script, Context);

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
        ERROR(TEXT("[ScriptCreateASTNode] Failed to allocate AST node"));
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
            if (Node->Data.Expression.BaseExpression) {
                ScriptDestroyAST(Node->Data.Expression.BaseExpression);
            }
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

static void ScriptValueInit(SCRIPT_VALUE* Value) {
    if (Value == NULL) {
        return;
    }

    MemorySet(Value, 0, sizeof(SCRIPT_VALUE));
    Value->Type = SCRIPT_VAR_FLOAT;
    Value->Value.Float = 0.0f;
    Value->OwnsValue = FALSE;
    Value->HostDescriptor = NULL;
    Value->HostContext = NULL;
}

/************************************************************************/

static void ScriptValueRelease(SCRIPT_VALUE* Value) {
    if (Value == NULL) {
        return;
    }

    if (Value->Type == SCRIPT_VAR_STRING && Value->OwnsValue && Value->Value.String) {
        HeapFree(Value->Value.String);
    } else if (Value->Type == SCRIPT_VAR_ARRAY && Value->OwnsValue && Value->Value.Array) {
        ScriptDestroyArray(Value->Value.Array);
    } else if (Value->Type == SCRIPT_VAR_HOST_HANDLE && Value->OwnsValue &&
               Value->Value.HostHandle && Value->HostDescriptor &&
               Value->HostDescriptor->ReleaseHandle) {
        LPVOID HostCtx = Value->HostContext ? Value->HostContext : Value->HostDescriptor->Context;
        Value->HostDescriptor->ReleaseHandle(HostCtx, Value->Value.HostHandle);
    }

    Value->Type = SCRIPT_VAR_FLOAT;
    Value->Value.Float = 0.0f;
    Value->OwnsValue = FALSE;
    Value->HostDescriptor = NULL;
    Value->HostContext = NULL;
}

/************************************************************************/

static U32 ScriptHashHostSymbol(LPCSTR Name) {
    return ScriptHashVariable(Name);
}

/************************************************************************/

static BOOL ScriptInitHostRegistry(LPSCRIPT_HOST_REGISTRY Registry) {
    if (Registry == NULL) {
        return FALSE;
    }

    MemorySet(Registry, 0, sizeof(SCRIPT_HOST_REGISTRY));

    for (U32 i = 0; i < SCRIPT_VAR_HASH_SIZE; i++) {
        Registry->Buckets[i] = NewList(NULL, HeapAlloc, HeapFree);
        if (Registry->Buckets[i] == NULL) {
            for (U32 j = 0; j < i; j++) {
                if (Registry->Buckets[j]) {
                    DeleteList(Registry->Buckets[j]);
                    Registry->Buckets[j] = NULL;
                }
            }
            return FALSE;
        }
    }

    Registry->Count = 0;
    return TRUE;
}

/************************************************************************/

static void ScriptReleaseHostSymbol(LPSCRIPT_HOST_SYMBOL Symbol) {
    if (Symbol == NULL) {
        return;
    }

    if (Symbol->Descriptor && Symbol->Descriptor->ReleaseHandle && Symbol->Handle) {
        LPVOID HostCtx = Symbol->Context ? Symbol->Context : Symbol->Descriptor->Context;
        Symbol->Descriptor->ReleaseHandle(HostCtx, Symbol->Handle);
    }

    HeapFree(Symbol);
}

/************************************************************************/

static void ScriptClearHostRegistryInternal(LPSCRIPT_HOST_REGISTRY Registry) {
    if (Registry == NULL) {
        return;
    }

    for (U32 i = 0; i < SCRIPT_VAR_HASH_SIZE; i++) {
        if (Registry->Buckets[i]) {
            LPSCRIPT_HOST_SYMBOL Symbol = (LPSCRIPT_HOST_SYMBOL)Registry->Buckets[i]->First;
            while (Symbol) {
                LPSCRIPT_HOST_SYMBOL Next = (LPSCRIPT_HOST_SYMBOL)Symbol->Next;
                ScriptReleaseHostSymbol(Symbol);
                Symbol = Next;
            }
            DeleteList(Registry->Buckets[i]);
            Registry->Buckets[i] = NULL;
        }
    }

    Registry->Count = 0;
}

/************************************************************************/

static LPSCRIPT_HOST_SYMBOL ScriptFindHostSymbol(LPSCRIPT_HOST_REGISTRY Registry, LPCSTR Name) {
    if (Registry == NULL || Name == NULL) {
        return NULL;
    }

    U32 Hash = ScriptHashHostSymbol(Name);
    LPLIST Bucket = Registry->Buckets[Hash];
    if (Bucket == NULL) {
        return NULL;
    }

    for (LPSCRIPT_HOST_SYMBOL Symbol = (LPSCRIPT_HOST_SYMBOL)Bucket->First;
         Symbol;
         Symbol = (LPSCRIPT_HOST_SYMBOL)Symbol->Next) {
        if (STRINGS_EQUAL(Symbol->Name, Name)) {
            return Symbol;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Initialize a script parser.
 * @param Parser Parser to initialize
 * @param Input Input string to parse
 * @param Variables Variable table
 * @param Callbacks Callback functions
 */
static void ScriptInitParser(LPSCRIPT_PARSER Parser, LPCSTR Input, LPSCRIPT_CONTEXT Context) {
    Parser->Input = Input;
    Parser->Position = 0;
    Parser->Variables = &Context->Variables;
    Parser->Callbacks = &Context->Callbacks;
    Parser->CurrentScope = Context->CurrentScope;
    Parser->Context = Context;

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
        ScriptParseStringToken(Parser, Input, Pos, '"');

    } else if (Ch == '\'') {
        ScriptParseStringToken(Parser, Input, Pos, '\'');

    } else if (Ch == '/') {
        BOOL TreatAsPath = TRUE;

        if (Input[*Pos + 1] == STR_NULL ||
            Input[*Pos + 1] == ' ' || Input[*Pos + 1] == '\t' ||
            Input[*Pos + 1] == '\n' || Input[*Pos + 1] == '\r') {
            TreatAsPath = FALSE;
        } else if (Input[*Pos + 1] == '/') {
            TreatAsPath = FALSE;
        }

        if (TreatAsPath) {
            BOOL HasValidStart = FALSE;

            if (*Pos == 0) {
                HasValidStart = TRUE;
            } else {
                I32 Prev = (I32)(*Pos) - 1;
                while (Prev >= 0) {
                    STR PrevCh = Input[Prev];
                    if (PrevCh == ' ' || PrevCh == '\t' || PrevCh == '\r') {
                        Prev--;
                        continue;
                    }

                    if (PrevCh == '\n' || PrevCh == ';' || PrevCh == '{' || PrevCh == '}') {
                        HasValidStart = TRUE;
                    } else {
                        HasValidStart = FALSE;
                    }
                    break;
                }

                if (Prev < 0) {
                    HasValidStart = TRUE;
                }
            }

            if (!HasValidStart) {
                TreatAsPath = FALSE;
            }
        }

        if (TreatAsPath) {
            Parser->CurrentToken.Type = TOKEN_PATH;
            U32 Start = *Pos;
            (*Pos)++;

            while (Input[*Pos] != STR_NULL) {
                STR Current = Input[*Pos];
                if (Current == ' ' || Current == '\t' || Current == '\n' ||
                    Current == '\r' || Current == ';') {
                    break;
                }
                (*Pos)++;
            }

            U32 Len = *Pos - Start;
            if (Len >= MAX_TOKEN_LENGTH) Len = MAX_TOKEN_LENGTH - 1;

            MemoryCopy(Parser->CurrentToken.Value, &Input[Start], Len);
            Parser->CurrentToken.Value[Len] = STR_NULL;
        } else {
            Parser->CurrentToken.Type = TOKEN_OPERATOR;
            Parser->CurrentToken.Value[0] = Ch;
            Parser->CurrentToken.Value[1] = STR_NULL;
            (*Pos)++;
        }

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
 * @brief Parse a string literal token and handle escape sequences.
 * @param Parser Parser state
 * @param Input Original script input
 * @param Pos Current position pointer in the input
 * @param QuoteChar Quote character that delimits the string
 */
static void ScriptParseStringToken(LPSCRIPT_PARSER Parser, LPCSTR Input, U32* Pos, STR QuoteChar) {
    Parser->CurrentToken.Type = TOKEN_STRING;
    (*Pos)++;

    U32 OutputIndex = 0;

    while (Input[*Pos] != STR_NULL) {
        STR Current = Input[*Pos];

        if (Current == QuoteChar) {
            (*Pos)++;
            break;
        }

        if (Current == '\\') {
            (*Pos)++;

            if (Input[*Pos] == STR_NULL) {
                if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
                    Parser->CurrentToken.Value[OutputIndex++] = '\\';
                }
                break;
            }

            STR Escaped = Input[*Pos];
            STR Resolved = STR_NULL;
            BOOL Recognized = TRUE;

            switch (Escaped) {
                case 'n':
                    Resolved = '\n';
                    break;
                case 'r':
                    Resolved = '\r';
                    break;
                case 't':
                    Resolved = '\t';
                    break;
                case '\\':
                    Resolved = '\\';
                    break;
                case '\'':
                    Resolved = '\'';
                    break;
                case '"':
                    Resolved = '"';
                    break;
                default:
                    Recognized = FALSE;
                    break;
            }

            if (Recognized) {
                if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
                    Parser->CurrentToken.Value[OutputIndex++] = Resolved;
                }
            } else {
                if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
                    Parser->CurrentToken.Value[OutputIndex++] = '\\';
                }
                if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
                    Parser->CurrentToken.Value[OutputIndex++] = Escaped;
                }
            }

            (*Pos)++;
            continue;
        }

        if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
            Parser->CurrentToken.Value[OutputIndex++] = Current;
        }
        (*Pos)++;
    }

    Parser->CurrentToken.Value[OutputIndex] = STR_NULL;
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
            *Error = SCRIPT_ERROR_SYNTAX;
            ScriptDestroyAST(Node);
            return NULL;
        }
        ScriptNextToken(Parser);
    }

    if (Parser->CurrentToken.Type != TOKEN_OPERATOR || Parser->CurrentToken.Value[0] != '=') {
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
                    *Error = SCRIPT_ERROR_SYNTAX;
                    ScriptDestroyAST(Node);
                    return NULL;
                }
                ScriptNextToken(Parser);
            }
        }
        LPAST_NODE CurrentNode = Node;
        BOOL ContinueParsing = TRUE;

        while (ContinueParsing) {
            ContinueParsing = FALSE;

            if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
                ScriptNextToken(Parser);

                LPAST_NODE IndexExpr = ScriptParseComparisonAST(Parser, Error);
                if (*Error != SCRIPT_OK || IndexExpr == NULL) {
                    ScriptDestroyAST(CurrentNode);
                    return NULL;
                }

                if (Parser->CurrentToken.Type != TOKEN_RBRACKET) {
                    *Error = SCRIPT_ERROR_SYNTAX;
                    ScriptDestroyAST(CurrentNode);
                    ScriptDestroyAST(IndexExpr);
                    return NULL;
                }
                ScriptNextToken(Parser);

                if (!CurrentNode->Data.Expression.IsArrayAccess && CurrentNode->Data.Expression.BaseExpression == NULL &&
                    CurrentNode == Node) {
                    CurrentNode->Data.Expression.IsArrayAccess = TRUE;
                    CurrentNode->Data.Expression.ArrayIndexExpr = IndexExpr;
                } else {
                    LPAST_NODE ArrayNode = ScriptCreateASTNode(AST_EXPRESSION);
                    if (ArrayNode == NULL) {
                        ScriptDestroyAST(IndexExpr);
                        ScriptDestroyAST(CurrentNode);
                        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                        return NULL;
                    }

                    ArrayNode->Data.Expression.TokenType = TOKEN_IDENTIFIER;
                    ArrayNode->Data.Expression.IsVariable = TRUE;
                    ArrayNode->Data.Expression.IsArrayAccess = TRUE;
                    ArrayNode->Data.Expression.BaseExpression = CurrentNode;
                    ArrayNode->Data.Expression.ArrayIndexExpr = IndexExpr;
                    CurrentNode = ArrayNode;
                }

                ContinueParsing = TRUE;
            } else if (Parser->CurrentToken.Type == TOKEN_OPERATOR && Parser->CurrentToken.Value[0] == '.') {
                ScriptNextToken(Parser);

                if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
                    *Error = SCRIPT_ERROR_SYNTAX;
                    ScriptDestroyAST(CurrentNode);
                    return NULL;
                }

                LPAST_NODE PropertyNode = ScriptCreateASTNode(AST_EXPRESSION);
                if (PropertyNode == NULL) {
                    ScriptDestroyAST(CurrentNode);
                    *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                    return NULL;
                }

                PropertyNode->Data.Expression.TokenType = TOKEN_IDENTIFIER;
                PropertyNode->Data.Expression.IsVariable = FALSE;
                PropertyNode->Data.Expression.IsPropertyAccess = TRUE;
                PropertyNode->Data.Expression.BaseExpression = CurrentNode;
                StringCopy(PropertyNode->Data.Expression.PropertyName, Parser->CurrentToken.Value);

                ScriptNextToken(Parser);

                CurrentNode = PropertyNode;
                ContinueParsing = TRUE;
            }
        }

        return CurrentNode;
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

BOOL ScriptRegisterHostSymbol(LPSCRIPT_CONTEXT Context, LPCSTR Name, SCRIPT_HOST_SYMBOL_KIND Kind, SCRIPT_HOST_HANDLE Handle, const SCRIPT_HOST_DESCRIPTOR* Descriptor, LPVOID ContextPointer) {
    if (Context == NULL || Name == NULL || Descriptor == NULL) {
        return FALSE;
    }

    if (Context->HostRegistry.Buckets[0] == NULL) {
        if (!ScriptInitHostRegistry(&Context->HostRegistry)) {
            return FALSE;
        }
    }

    U32 Hash = ScriptHashHostSymbol(Name);
    LPLIST Bucket = Context->HostRegistry.Buckets[Hash];
    if (Bucket == NULL) {
        Bucket = NewList(NULL, HeapAlloc, HeapFree);
        if (Bucket == NULL) {
            return FALSE;
        }
        Context->HostRegistry.Buckets[Hash] = Bucket;
    }

    LPSCRIPT_HOST_SYMBOL Existing = ScriptFindHostSymbol(&Context->HostRegistry, Name);
    if (Existing) {
        ListRemove(Bucket, Existing);
        ScriptReleaseHostSymbol(Existing);
        if (Context->HostRegistry.Count > 0) {
            Context->HostRegistry.Count--;
        }
    }

    LPSCRIPT_HOST_SYMBOL Symbol = (LPSCRIPT_HOST_SYMBOL)HeapAlloc(sizeof(SCRIPT_HOST_SYMBOL));
    if (Symbol == NULL) {
        return FALSE;
    }

    MemorySet(Symbol, 0, sizeof(SCRIPT_HOST_SYMBOL));
    StringCopy(Symbol->Name, Name);
    Symbol->Kind = Kind;
    Symbol->Handle = Handle;
    Symbol->Descriptor = Descriptor;
    Symbol->Context = ContextPointer;

    ListAddItem(Bucket, Symbol);
    Context->HostRegistry.Count++;

    return TRUE;
}

/************************************************************************/

void ScriptUnregisterHostSymbol(LPSCRIPT_CONTEXT Context, LPCSTR Name) {
    if (Context == NULL || Name == NULL) {
        return;
    }

    if (Context->HostRegistry.Buckets[0] == NULL) {
        return;
    }

    U32 Hash = ScriptHashHostSymbol(Name);
    LPLIST Bucket = Context->HostRegistry.Buckets[Hash];
    if (Bucket == NULL) {
        return;
    }

    for (LPSCRIPT_HOST_SYMBOL Symbol = (LPSCRIPT_HOST_SYMBOL)Bucket->First;
         Symbol;
         Symbol = (LPSCRIPT_HOST_SYMBOL)Symbol->Next) {
        if (STRINGS_EQUAL(Symbol->Name, Name)) {
            ListRemove(Bucket, Symbol);
            ScriptReleaseHostSymbol(Symbol);
            if (Context->HostRegistry.Count > 0) {
                Context->HostRegistry.Count--;
            }
            return;
        }
    }
}

/************************************************************************/

void ScriptClearHostSymbols(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL) {
        return;
    }

    ScriptClearHostRegistryInternal(&Context->HostRegistry);
    ScriptInitHostRegistry(&Context->HostRegistry);
}

/************************************************************************/

static SCRIPT_ERROR ScriptPrepareHostValue(SCRIPT_VALUE* Value, const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor, LPVOID DefaultContext) {
    if (Value == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (Value->Type == SCRIPT_VAR_STRING && Value->Value.String && !Value->OwnsValue) {
        U32 Len = StringLength(Value->Value.String) + 1;
        LPSTR Copy = (LPSTR)HeapAlloc(Len);
        if (Copy == NULL) {
            return SCRIPT_ERROR_OUT_OF_MEMORY;
        }
        StringCopy(Copy, Value->Value.String);
        Value->Value.String = Copy;
        Value->OwnsValue = TRUE;
    }

    if (Value->Type == SCRIPT_VAR_HOST_HANDLE) {
        if (Value->HostDescriptor == NULL) {
            Value->HostDescriptor = DefaultDescriptor;
        }
        if (Value->HostContext == NULL) {
            Value->HostContext = DefaultContext;
        }
    }

    return SCRIPT_OK;
}

/************************************************************************/

static BOOL ScriptValueToFloat(const SCRIPT_VALUE* Value, F32* OutValue) {
    if (Value == NULL || OutValue == NULL) {
        return FALSE;
    }

    if (Value->Type == SCRIPT_VAR_FLOAT) {
        *OutValue = Value->Value.Float;
        return TRUE;
    }

    if (Value->Type == SCRIPT_VAR_INTEGER) {
        *OutValue = (F32)Value->Value.Integer;
        return TRUE;
    }

    return FALSE;
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
static SCRIPT_VALUE ScriptEvaluateExpression(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
    SCRIPT_VALUE Result;
    ScriptValueInit(&Result);

    if (Error) {
        *Error = SCRIPT_OK;
    }

    if (Expr == NULL || Expr->Type != AST_EXPRESSION) {
        if (Error) {
            *Error = SCRIPT_ERROR_SYNTAX;
        }
        return Result;
    }

    if (Expr->Data.Expression.IsPropertyAccess) {
        return ScriptEvaluateHostProperty(Parser, Expr, Error);
    }

    if (Expr->Data.Expression.IsArrayAccess && Expr->Data.Expression.BaseExpression) {
        return ScriptEvaluateArrayAccess(Parser, Expr, Error);
    }

    switch (Expr->Data.Expression.TokenType) {
        case TOKEN_NUMBER:
            Result.Type = SCRIPT_VAR_FLOAT;
            Result.Value.Float = Expr->Data.Expression.NumValue;
            return Result;

        case TOKEN_STRING: {
            U32 Length = StringLength(Expr->Data.Expression.Value) + 1;
            Result.Value.String = (LPSTR)HeapAlloc(Length);
            if (Result.Value.String == NULL) {
                if (Error) {
                    *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                }
                return Result;
            }
            StringCopy(Result.Value.String, Expr->Data.Expression.Value);
            Result.Type = SCRIPT_VAR_STRING;
            Result.OwnsValue = TRUE;
            return Result;
        }

        case TOKEN_IDENTIFIER:
        case TOKEN_PATH: {
            if (Expr->Data.Expression.IsFunctionCall) {
                if (Expr->Data.Expression.IsShellCommand) {
                    if (Parser->Callbacks && Parser->Callbacks->ExecuteCommand) {
                        LPCSTR CommandLine = Expr->Data.Expression.CommandLine ?
                            Expr->Data.Expression.CommandLine : Expr->Data.Expression.Value;
                        U32 Status = Parser->Callbacks->ExecuteCommand(CommandLine, Parser->Callbacks->UserData);

                        if (Status == DF_ERROR_SUCCESS) {
                            Result.Type = SCRIPT_VAR_FLOAT;
                            Result.Value.Float = (F32)Status;
                            return Result;
                        }

                        LPSCRIPT_CONTEXT Context = Parser->Context;
                        if (Context) {
                            Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                            if (Context->ErrorMessage[0] == STR_NULL) {
                                StringPrintFormat(Context->ErrorMessage, TEXT("Command failed (0x%08X)"), Status);
                            }
                        }

                        if (Error) {
                            *Error = SCRIPT_ERROR_SYNTAX;
                        }
                        return Result;
                    }

                    LPSCRIPT_CONTEXT Context = Parser->Context;
                    if (Context) {
                        Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                        if (Context->ErrorMessage[0] == STR_NULL) {
                            StringCopy(Context->ErrorMessage, TEXT("No command callback registered"));
                        }
                    }

                    if (Error) {
                        *Error = SCRIPT_ERROR_SYNTAX;
                    }
                    return Result;
                }

                if (Expr->Data.Expression.TokenType == TOKEN_PATH) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_SYNTAX;
                    }
                    return Result;
                }

                if (Parser->Callbacks && Parser->Callbacks->CallFunction) {
                    LPCSTR ArgString = TEXT("");
                    STR ArgBuffer[MAX_TOKEN_LENGTH];
                    SCRIPT_VALUE ArgValue;
                    BOOL HasEvaluatedArg = FALSE;

                    if (Expr->Data.Expression.Left) {
                        if (Expr->Data.Expression.Left->Data.Expression.TokenType == TOKEN_STRING) {
                            ArgString = Expr->Data.Expression.Left->Data.Expression.Value;
                        } else {
                            ArgValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Left, Error);
                            HasEvaluatedArg = TRUE;

                            if (Error && *Error != SCRIPT_OK) {
                                ScriptValueRelease(&ArgValue);
                                return Result;
                            }

                            if (ArgValue.Type == SCRIPT_VAR_STRING) {
                                ArgString = ArgValue.Value.String ? ArgValue.Value.String : TEXT("");
                            } else {
                                F32 ArgNumeric;
                                if (!ScriptValueToFloat(&ArgValue, &ArgNumeric)) {
                                    if (Error) {
                                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                                    }
                                    ScriptValueRelease(&ArgValue);
                                    return Result;
                                }

                                if (IsInteger(ArgNumeric)) {
                                    StringPrintFormat(ArgBuffer, TEXT("%d"), (I32)ArgNumeric);
                                } else {
                                    StringPrintFormat(ArgBuffer, TEXT("%f"), ArgNumeric);
                                }

                                ArgString = ArgBuffer;
                            }
                        }
                    }

                    U32 Status = Parser->Callbacks->CallFunction(
                        Expr->Data.Expression.Value,
                        ArgString,
                        Parser->Callbacks->UserData);

                    if (HasEvaluatedArg) {
                        ScriptValueRelease(&ArgValue);
                    }

                    Result.Type = SCRIPT_VAR_FLOAT;
                    Result.Value.Float = (F32)Status;
                    return Result;
                }

                LPSCRIPT_CONTEXT Context = Parser->Context;
                if (Context) {
                    Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                    if (Context->ErrorMessage[0] == STR_NULL) {
                        StringCopy(Context->ErrorMessage, TEXT("No function callback registered"));
                    }
                }

                if (Error) {
                    *Error = SCRIPT_ERROR_SYNTAX;
                }
                return Result;
            }

            if (Expr->Data.Expression.IsArrayAccess && Expr->Data.Expression.BaseExpression == NULL) {
                SCRIPT_VALUE IndexValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.ArrayIndexExpr, Error);
                if (Error && *Error != SCRIPT_OK) {
                    ScriptValueRelease(&IndexValue);
                    return Result;
                }

                F32 IndexNumeric;
                if (!ScriptValueToFloat(&IndexValue, &IndexNumeric)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&IndexValue);
                    return Result;
                }

                U32 ArrayIndex = (U32)IndexNumeric;
                ScriptValueRelease(&IndexValue);

                LPSCRIPT_HOST_SYMBOL HostArray = ScriptFindHostSymbol(&Parser->Context->HostRegistry, Expr->Data.Expression.Value);
                if (HostArray) {
                    if (HostArray->Descriptor == NULL || HostArray->Descriptor->GetElement == NULL) {
                        if (Error) {
                            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                        }
                        return Result;
                    }

                    SCRIPT_VALUE HostValue;
                    ScriptValueInit(&HostValue);
                    LPVOID HostCtx = HostArray->Context ? HostArray->Context : HostArray->Descriptor->Context;
                    SCRIPT_ERROR HostError = HostArray->Descriptor->GetElement(HostCtx, HostArray->Handle, ArrayIndex, &HostValue);
                    if (HostError != SCRIPT_OK) {
                        if (Error) {
                            *Error = HostError;
                        }
                        ScriptValueRelease(&HostValue);
                        return Result;
                    }

                    HostError = ScriptPrepareHostValue(&HostValue, HostArray->Descriptor, HostCtx);
                    if (HostError != SCRIPT_OK) {
                        if (Error) {
                            *Error = HostError;
                        }
                        ScriptValueRelease(&HostValue);
                        return Result;
                    }

                    return HostValue;
                }

                LPSCRIPT_VARIABLE Element = ScriptGetArrayElement(Parser->Context, Expr->Data.Expression.Value, ArrayIndex);
                if (Element == NULL) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_UNDEFINED_VAR;
                    }
                    return Result;
                }

                Result.Type = Element->Type;
                Result.Value = Element->Value;
                Result.OwnsValue = FALSE;

                HeapFree(Element);
                return Result;
            }

            LPSCRIPT_HOST_SYMBOL HostSymbol = ScriptFindHostSymbol(&Parser->Context->HostRegistry, Expr->Data.Expression.Value);
            if (HostSymbol) {
                LPVOID HostCtx = HostSymbol->Context ? HostSymbol->Context : HostSymbol->Descriptor->Context;

                if (HostSymbol->Kind == SCRIPT_HOST_SYMBOL_PROPERTY) {
                    if (HostSymbol->Descriptor == NULL || HostSymbol->Descriptor->GetProperty == NULL) {
                        if (Error) {
                            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                        }
                        return Result;
                    }

                    SCRIPT_VALUE HostValue;
                    ScriptValueInit(&HostValue);
                    SCRIPT_ERROR HostError = HostSymbol->Descriptor->GetProperty(
                        HostCtx,
                        HostSymbol->Handle,
                        HostSymbol->Name,
                        &HostValue);
                    if (HostError != SCRIPT_OK) {
                        if (Error) {
                            *Error = HostError;
                        }
                        ScriptValueRelease(&HostValue);
                        return Result;
                    }

                    HostError = ScriptPrepareHostValue(&HostValue, HostSymbol->Descriptor, HostCtx);
                    if (HostError != SCRIPT_OK) {
                        if (Error) {
                            *Error = HostError;
                        }
                        ScriptValueRelease(&HostValue);
                        return Result;
                    }

                    return HostValue;
                }

                Result.Type = SCRIPT_VAR_HOST_HANDLE;
                Result.Value.HostHandle = HostSymbol->Handle;
                Result.HostDescriptor = HostSymbol->Descriptor;
                Result.HostContext = HostCtx;
                Result.OwnsValue = FALSE;
                return Result;
            }

            if (Expr->Data.Expression.TokenType == TOKEN_PATH) {
                if (Error) {
                    *Error = SCRIPT_ERROR_SYNTAX;
                }
                return Result;
            }

            LPSCRIPT_VARIABLE Variable = ScriptFindVariableInScope(Parser->CurrentScope, Expr->Data.Expression.Value, TRUE);
            if (Variable == NULL) {
                if (Error) {
                    *Error = SCRIPT_ERROR_UNDEFINED_VAR;
                }
                return Result;
            }

            if (Variable->Type == SCRIPT_VAR_INTEGER) {
                Result.Type = SCRIPT_VAR_INTEGER;
                Result.Value.Integer = Variable->Value.Integer;
                return Result;
            }

            if (Variable->Type == SCRIPT_VAR_FLOAT) {
                Result.Type = SCRIPT_VAR_FLOAT;
                Result.Value.Float = Variable->Value.Float;
                return Result;
            }

            if (Variable->Type == SCRIPT_VAR_STRING) {
                Result.Type = SCRIPT_VAR_STRING;
                Result.Value.String = Variable->Value.String;
                Result.OwnsValue = FALSE;
                return Result;
            }

            if (Error) {
                *Error = SCRIPT_ERROR_TYPE_MISMATCH;
            }
            return Result;
        }

        case TOKEN_OPERATOR:
        case TOKEN_COMPARISON: {
            SCRIPT_VALUE LeftValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Left, Error);
            if (Error && *Error != SCRIPT_OK) {
                ScriptValueRelease(&LeftValue);
                return Result;
            }

            SCRIPT_VALUE RightValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Right, Error);
            if (Error && *Error != SCRIPT_OK) {
                ScriptValueRelease(&LeftValue);
                ScriptValueRelease(&RightValue);
                return Result;
            }

            F32 LeftNumeric;
            F32 RightNumeric;

            if (!ScriptValueToFloat(&LeftValue, &LeftNumeric) ||
                !ScriptValueToFloat(&RightValue, &RightNumeric)) {
                if (Error) {
                    *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                }
                ScriptValueRelease(&LeftValue);
                ScriptValueRelease(&RightValue);
                return Result;
            }

            Result.Type = SCRIPT_VAR_FLOAT;

            if (Expr->Data.Expression.TokenType == TOKEN_OPERATOR) {
                STR Operator = Expr->Data.Expression.Value[0];

                if (Operator == '+') {
                    Result.Value.Float = LeftNumeric + RightNumeric;
                } else if (Operator == '-') {
                    Result.Value.Float = LeftNumeric - RightNumeric;
                } else if (Operator == '*') {
                    Result.Value.Float = LeftNumeric * RightNumeric;
                } else if (Operator == '/') {
                    if (RightNumeric == 0.0f) {
                        if (Error) {
                            *Error = SCRIPT_ERROR_DIVISION_BY_ZERO;
                        }
                        ScriptValueRelease(&LeftValue);
                        ScriptValueRelease(&RightValue);
                        return Result;
                    }

                    if (IsInteger(LeftNumeric) && IsInteger(RightNumeric)) {
                        Result.Value.Float = (F32)((I32)LeftNumeric / (I32)RightNumeric);
                    } else {
                        Result.Value.Float = LeftNumeric / RightNumeric;
                    }
                } else {
                    if (Error) {
                        *Error = SCRIPT_ERROR_SYNTAX;
                    }
                }
            } else {
                if (StringCompare(Expr->Data.Expression.Value, TEXT("<")) == 0) {
                    Result.Value.Float = (LeftNumeric < RightNumeric) ? 1.0f : 0.0f;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT("<=")) == 0) {
                    Result.Value.Float = (LeftNumeric <= RightNumeric) ? 1.0f : 0.0f;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT(">")) == 0) {
                    Result.Value.Float = (LeftNumeric > RightNumeric) ? 1.0f : 0.0f;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT(">=")) == 0) {
                    Result.Value.Float = (LeftNumeric >= RightNumeric) ? 1.0f : 0.0f;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT("==")) == 0) {
                    Result.Value.Float = (LeftNumeric == RightNumeric) ? 1.0f : 0.0f;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT("!=")) == 0) {
                    Result.Value.Float = (LeftNumeric != RightNumeric) ? 1.0f : 0.0f;
                } else {
                    if (Error) {
                        *Error = SCRIPT_ERROR_SYNTAX;
                    }
                }
            }

            ScriptValueRelease(&LeftValue);
            ScriptValueRelease(&RightValue);
            return Result;
        }

        default:
            if (Error) {
                *Error = SCRIPT_ERROR_SYNTAX;
            }
            return Result;
    }
}

/************************************************************************/

static SCRIPT_VALUE ScriptEvaluateHostProperty(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
    SCRIPT_VALUE Result;
    ScriptValueInit(&Result);

    SCRIPT_VALUE BaseValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.BaseExpression, Error);
    if (Error && *Error != SCRIPT_OK) {
        ScriptValueRelease(&BaseValue);
        return Result;
    }

    if (BaseValue.Type != SCRIPT_VAR_HOST_HANDLE || BaseValue.HostDescriptor == NULL ||
        BaseValue.HostDescriptor->GetProperty == NULL) {
        if (Error) {
            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
        }
        ScriptValueRelease(&BaseValue);
        return Result;
    }

    LPVOID HostCtx = BaseValue.HostContext ? BaseValue.HostContext : BaseValue.HostDescriptor->Context;
    const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor = BaseValue.HostDescriptor;

    SCRIPT_VALUE HostValue;
    ScriptValueInit(&HostValue);
    SCRIPT_ERROR HostError = BaseValue.HostDescriptor->GetProperty(
        HostCtx,
        BaseValue.Value.HostHandle,
        Expr->Data.Expression.PropertyName,
        &HostValue);

    ScriptValueRelease(&BaseValue);

    if (HostError != SCRIPT_OK) {
        if (Error) {
            *Error = HostError;
        }
        ScriptValueRelease(&HostValue);
        return Result;
    }

    HostError = ScriptPrepareHostValue(&HostValue, HostValue.HostDescriptor ? HostValue.HostDescriptor : DefaultDescriptor, HostCtx);
    if (HostError != SCRIPT_OK) {
        if (Error) {
            *Error = HostError;
        }
        ScriptValueRelease(&HostValue);
        return Result;
    }

    if (HostValue.Type == SCRIPT_VAR_HOST_HANDLE && HostValue.HostDescriptor == NULL) {
        HostValue.HostDescriptor = DefaultDescriptor;
    }
    if (HostValue.Type == SCRIPT_VAR_HOST_HANDLE && HostValue.HostContext == NULL) {
        HostValue.HostContext = HostCtx;
    }

    return HostValue;
}

/************************************************************************/

static SCRIPT_VALUE ScriptEvaluateArrayAccess(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
    SCRIPT_VALUE Result;
    ScriptValueInit(&Result);

    SCRIPT_VALUE BaseValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.BaseExpression, Error);
    if (Error && *Error != SCRIPT_OK) {
        ScriptValueRelease(&BaseValue);
        return Result;
    }

    SCRIPT_VALUE IndexValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.ArrayIndexExpr, Error);
    if (Error && *Error != SCRIPT_OK) {
        ScriptValueRelease(&BaseValue);
        ScriptValueRelease(&IndexValue);
        return Result;
    }

    F32 IndexNumeric;
    if (!ScriptValueToFloat(&IndexValue, &IndexNumeric)) {
        if (Error) {
            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
        }
        ScriptValueRelease(&BaseValue);
        ScriptValueRelease(&IndexValue);
        return Result;
    }

    ScriptValueRelease(&IndexValue);

    if (BaseValue.Type == SCRIPT_VAR_HOST_HANDLE &&
        BaseValue.HostDescriptor && BaseValue.HostDescriptor->GetElement) {
        LPVOID HostCtx = BaseValue.HostContext ? BaseValue.HostContext : BaseValue.HostDescriptor->Context;
        const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor = BaseValue.HostDescriptor;

        SCRIPT_VALUE HostValue;
        ScriptValueInit(&HostValue);
        SCRIPT_ERROR HostError = BaseValue.HostDescriptor->GetElement(
            HostCtx,
            BaseValue.Value.HostHandle,
            (U32)IndexNumeric,
            &HostValue);

        ScriptValueRelease(&BaseValue);

        if (HostError != SCRIPT_OK) {
            if (Error) {
                *Error = HostError;
            }
            ScriptValueRelease(&HostValue);
            return Result;
        }

        HostError = ScriptPrepareHostValue(&HostValue, DefaultDescriptor, HostCtx);
        if (HostError != SCRIPT_OK) {
            if (Error) {
                *Error = HostError;
            }
            ScriptValueRelease(&HostValue);
            return Result;
        }

        if (HostValue.Type == SCRIPT_VAR_HOST_HANDLE && HostValue.HostDescriptor == NULL) {
            HostValue.HostDescriptor = DefaultDescriptor;
        }
        if (HostValue.Type == SCRIPT_VAR_HOST_HANDLE && HostValue.HostContext == NULL) {
            HostValue.HostContext = HostCtx;
        }

        return HostValue;
    }

    ScriptValueRelease(&BaseValue);

    if (Error) {
        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
    }
    return Result;
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

    // Prevent assignment to host-exposed identifiers
    if (ScriptFindHostSymbol(&Parser->Context->HostRegistry, Node->Data.Assignment.VarName)) {
        return SCRIPT_ERROR_SYNTAX;
    }

    // Evaluate expression
    SCRIPT_ERROR Error = SCRIPT_OK;
    SCRIPT_VALUE EvaluatedValue = ScriptEvaluateExpression(Parser, Node->Data.Assignment.Expression, &Error);
    if (Error != SCRIPT_OK) {
        ScriptValueRelease(&EvaluatedValue);
        return Error;
    }

    if (EvaluatedValue.Type == SCRIPT_VAR_HOST_HANDLE) {
        ScriptValueRelease(&EvaluatedValue);
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    SCRIPT_VAR_VALUE VarValue;
    SCRIPT_VAR_TYPE VarType;

    if (EvaluatedValue.Type == SCRIPT_VAR_STRING) {
        VarType = SCRIPT_VAR_STRING;
        VarValue.String = EvaluatedValue.Value.String;
    } else if (EvaluatedValue.Type == SCRIPT_VAR_INTEGER) {
        VarType = SCRIPT_VAR_INTEGER;
        VarValue.Integer = EvaluatedValue.Value.Integer;
    } else {
        F32 Numeric = (EvaluatedValue.Type == SCRIPT_VAR_FLOAT) ? EvaluatedValue.Value.Float : 0.0f;
        if (IsInteger(Numeric)) {
            VarType = SCRIPT_VAR_INTEGER;
            VarValue.Integer = (I32)Numeric;
        } else {
            VarType = SCRIPT_VAR_FLOAT;
            VarValue.Float = Numeric;
        }
    }

    LPSCRIPT_CONTEXT Context = Parser->Context;

    if (Node->Data.Assignment.IsArrayAccess) {
        // Evaluate array index
        SCRIPT_VALUE IndexValue = ScriptEvaluateExpression(Parser, Node->Data.Assignment.ArrayIndexExpr, &Error);
        if (Error != SCRIPT_OK) {
            ScriptValueRelease(&EvaluatedValue);
            ScriptValueRelease(&IndexValue);
            return Error;
        }

        F32 IndexNumeric;
        if (!ScriptValueToFloat(&IndexValue, &IndexNumeric)) {
            ScriptValueRelease(&EvaluatedValue);
            ScriptValueRelease(&IndexValue);
            return SCRIPT_ERROR_TYPE_MISMATCH;
        }

        U32 ArrayIndex = (U32)IndexNumeric;
        ScriptValueRelease(&IndexValue);

        // Set array element
        if (ScriptSetArrayElement(Context, Node->Data.Assignment.VarName, ArrayIndex, VarType, VarValue) == NULL) {
            ScriptValueRelease(&EvaluatedValue);
            return SCRIPT_ERROR_SYNTAX;
        }
    } else {
        // Set regular variable in current scope
        if (ScriptSetVariableInScope(Parser->CurrentScope, Node->Data.Assignment.VarName, VarType, VarValue) == NULL) {
            ScriptValueRelease(&EvaluatedValue);
            return SCRIPT_ERROR_SYNTAX;
        }
    }

    ScriptValueRelease(&EvaluatedValue);

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
            SCRIPT_VALUE ConditionValue = ScriptEvaluateExpression(Parser, Node->Data.If.Condition, &Error);
            if (Error != SCRIPT_OK) {
                ScriptValueRelease(&ConditionValue);
                return Error;
            }

            F32 ConditionNumeric;
            if (!ScriptValueToFloat(&ConditionValue, &ConditionNumeric)) {
                ScriptValueRelease(&ConditionValue);
                return SCRIPT_ERROR_TYPE_MISMATCH;
            }

            ScriptValueRelease(&ConditionValue);

            // Execute then or else branch
            if (ConditionNumeric != 0.0f) {
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
                SCRIPT_VALUE ConditionValue = ScriptEvaluateExpression(Parser, Node->Data.For.Condition, &Error);
                if (Error != SCRIPT_OK) {
                    ScriptValueRelease(&ConditionValue);
                    return Error;
                }

                F32 ConditionNumeric;
                if (!ScriptValueToFloat(&ConditionValue, &ConditionNumeric)) {
                    ScriptValueRelease(&ConditionValue);
                    return SCRIPT_ERROR_TYPE_MISMATCH;
                }

                ScriptValueRelease(&ConditionValue);

                if (ConditionNumeric == 0.0f) break;

                // Execute body
                Error = ScriptExecuteAST(Parser, Node->Data.For.Body);
                if (Error != SCRIPT_OK) return Error;

                // Execute increment
                Error = ScriptExecuteAST(Parser, Node->Data.For.Increment);
                if (Error != SCRIPT_OK) return Error;

                LoopCount++;
            }

            if (LoopCount >= MAX_ITERATIONS) {
                ERROR(TEXT("[ScriptExecuteAST] Loop exceeded maximum iterations"));
            }

            return SCRIPT_OK;
        }

        case AST_EXPRESSION: {
            // Standalone expression - evaluate it (for function calls)
            SCRIPT_ERROR Error = SCRIPT_OK;
            SCRIPT_VALUE Temp = ScriptEvaluateExpression(Parser, Node, &Error);
            ScriptValueRelease(&Temp);
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
    } else if (Parser->CurrentToken.Type == TOKEN_PATH) {
        return ScriptParseShellCommandExpression(Parser, Error);
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

    if (Parser->CurrentToken.Type == TOKEN_PATH) {
        return TRUE;
    }

    if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
        return FALSE;
    }

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
    TOKEN_TYPE InitialTokenType = Parser->CurrentToken.Type;

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

    Node->Data.Expression.TokenType = (InitialTokenType == TOKEN_PATH) ? TOKEN_PATH : TOKEN_IDENTIFIER;
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
