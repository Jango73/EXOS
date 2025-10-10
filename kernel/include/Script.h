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

#pragma once

#include "Base.h"
#include "List.h"

/************************************************************************/

#define MAX_VAR_NAME 64
#define MAX_TOKEN_LENGTH 128
#define MAX_ERROR_MESSAGE 256
#define SCRIPT_VAR_HASH_SIZE 32

/************************************************************************/

typedef enum {
    SCRIPT_VAR_STRING,
    SCRIPT_VAR_INTEGER,
    SCRIPT_VAR_FLOAT,
    SCRIPT_VAR_ARRAY,
    SCRIPT_VAR_HOST_HANDLE
} SCRIPT_VAR_TYPE;

typedef struct {
    LPVOID* Elements;
    SCRIPT_VAR_TYPE* ElementTypes;
    UINT Size;
    UINT Capacity;
} SCRIPT_ARRAY, *LPSCRIPT_ARRAY;

typedef union {
    LPSTR String;
    I32 Integer;
    F32 Float;
    LPSCRIPT_ARRAY Array;
    LPVOID HostHandle;
} SCRIPT_VAR_VALUE;

typedef struct tag_SCRIPT_VARIABLE {
    LISTNODE_FIELDS;
    STR Name[MAX_VAR_NAME];
    SCRIPT_VAR_TYPE Type;
    SCRIPT_VAR_VALUE Value;
    U32 RefCount;
} SCRIPT_VARIABLE, *LPSCRIPT_VARIABLE;

typedef struct tag_SCRIPT_SCOPE {
    LPLIST Buckets[SCRIPT_VAR_HASH_SIZE];
    U32 Count;
    struct tag_SCRIPT_SCOPE* Parent;
    U32 ScopeLevel;
} SCRIPT_SCOPE, *LPSCRIPT_SCOPE;

typedef struct {
    LPLIST Buckets[SCRIPT_VAR_HASH_SIZE];
    U32 Count;
} SCRIPT_VAR_TABLE, *LPSCRIPT_VAR_TABLE;

/************************************************************************/

typedef enum {
    SCRIPT_OK = 0,
    SCRIPT_ERROR_SYNTAX,
    SCRIPT_ERROR_UNDEFINED_VAR,
    SCRIPT_ERROR_TYPE_MISMATCH,
    SCRIPT_ERROR_DIVISION_BY_ZERO,
    SCRIPT_ERROR_OUT_OF_MEMORY,
    SCRIPT_ERROR_UNMATCHED_BRACE
} SCRIPT_ERROR;

/************************************************************************/

typedef LPVOID SCRIPT_HOST_HANDLE;

struct tag_SCRIPT_VALUE;
struct tag_SCRIPT_HOST_DESCRIPTOR;

typedef enum {
    SCRIPT_HOST_SYMBOL_PROPERTY,
    SCRIPT_HOST_SYMBOL_ARRAY,
    SCRIPT_HOST_SYMBOL_OBJECT
} SCRIPT_HOST_SYMBOL_KIND;

typedef SCRIPT_ERROR (*SCRIPT_HOST_GET_PROPERTY)(LPVOID Context, SCRIPT_HOST_HANDLE Parent, LPCSTR Property, struct tag_SCRIPT_VALUE* OutValue);
typedef SCRIPT_ERROR (*SCRIPT_HOST_GET_ELEMENT)(LPVOID Context, SCRIPT_HOST_HANDLE Parent, U32 Index, struct tag_SCRIPT_VALUE* OutValue);
typedef void (*SCRIPT_HOST_RELEASE_HANDLE)(LPVOID Context, SCRIPT_HOST_HANDLE Handle);

typedef struct tag_SCRIPT_HOST_DESCRIPTOR {
    SCRIPT_HOST_GET_PROPERTY GetProperty;
    SCRIPT_HOST_GET_ELEMENT GetElement;
    SCRIPT_HOST_RELEASE_HANDLE ReleaseHandle;
    LPVOID Context;
} SCRIPT_HOST_DESCRIPTOR, *LPSCRIPT_HOST_DESCRIPTOR;

typedef struct tag_SCRIPT_VALUE {
    SCRIPT_VAR_TYPE Type;
    SCRIPT_VAR_VALUE Value;
    const SCRIPT_HOST_DESCRIPTOR* HostDescriptor;
    BOOL OwnsValue;
    LPVOID HostContext;
} SCRIPT_VALUE, *LPSCRIPT_VALUE;

typedef struct tag_SCRIPT_HOST_SYMBOL {
    LISTNODE_FIELDS;
    STR Name[MAX_VAR_NAME];
    SCRIPT_HOST_SYMBOL_KIND Kind;
    SCRIPT_HOST_HANDLE Handle;
    const SCRIPT_HOST_DESCRIPTOR* Descriptor;
    LPVOID Context;
} SCRIPT_HOST_SYMBOL, *LPSCRIPT_HOST_SYMBOL;

typedef struct {
    LPLIST Buckets[SCRIPT_VAR_HASH_SIZE];
    U32 Count;
} SCRIPT_HOST_REGISTRY, *LPSCRIPT_HOST_REGISTRY;

/************************************************************************/

typedef enum {
    TOKEN_EOF,
    TOKEN_IDENTIFIER,
    TOKEN_PATH,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_OPERATOR,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_SEMICOLON,
    TOKEN_COMPARISON,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR
} TOKEN_TYPE;

typedef struct {
    TOKEN_TYPE Type;
    STR Value[MAX_TOKEN_LENGTH];
    F32 NumValue;
    U32 Position;
    U32 Line;
    U32 Column;
} SCRIPT_TOKEN, *LPSCRIPT_TOKEN;

/************************************************************************/

typedef void (*SCRIPT_OUTPUT_CALLBACK)(LPCSTR Message, LPVOID UserData);
typedef U32 (*SCRIPT_COMMAND_CALLBACK)(LPCSTR Command, LPVOID UserData);
typedef LPCSTR (*SCRIPT_VARIABLE_RESOLVER)(LPCSTR VarName, LPVOID UserData);
typedef U32 (*SCRIPT_FUNCTION_CALLBACK)(LPCSTR FuncName, LPCSTR Argument, LPVOID UserData);

typedef struct {
    SCRIPT_OUTPUT_CALLBACK Output;
    SCRIPT_COMMAND_CALLBACK ExecuteCommand;
    SCRIPT_VARIABLE_RESOLVER ResolveVariable;
    SCRIPT_FUNCTION_CALLBACK CallFunction;
    LPVOID UserData;
} SCRIPT_CALLBACKS, *LPSCRIPT_CALLBACKS;

/************************************************************************/

// AST Node Types
typedef enum {
    AST_ASSIGNMENT,     // var = expr
    AST_IF,             // if (cond) then [else]
    AST_FOR,            // for (init; cond; inc) body
    AST_BLOCK,          // { statements }
    AST_EXPRESSION      // standalone expr
} AST_NODE_TYPE;

// Forward declaration
struct tag_AST_NODE;

// AST Node structure
typedef struct tag_AST_NODE {
    AST_NODE_TYPE Type;
    union {
        struct {
            STR VarName[MAX_VAR_NAME];
            struct tag_AST_NODE* Expression;
            BOOL IsArrayAccess;
            UINT ArrayIndex;
            struct tag_AST_NODE* ArrayIndexExpr;
        } Assignment;
        struct {
            struct tag_AST_NODE* Condition;
            struct tag_AST_NODE* Then;
            struct tag_AST_NODE* Else;
        } If;
        struct {
            struct tag_AST_NODE* Init;
            struct tag_AST_NODE* Condition;
            struct tag_AST_NODE* Increment;
            struct tag_AST_NODE* Body;
        } For;
        struct {
            struct tag_AST_NODE** Statements;
            U32 Count;
            U32 Capacity;
        } Block;
        struct {
            TOKEN_TYPE TokenType;
            STR Value[MAX_TOKEN_LENGTH];
            F32 NumValue;
            BOOL IsVariable;
            BOOL IsArrayAccess;
            UINT ArrayIndex;
            struct tag_AST_NODE* ArrayIndexExpr;
            struct tag_AST_NODE* BaseExpression;
            BOOL IsPropertyAccess;
            STR PropertyName[MAX_TOKEN_LENGTH];
            BOOL IsFunctionCall;
            STR Argument[MAX_TOKEN_LENGTH];
            struct tag_AST_NODE* Left;   // Left operand for binary operations, or function argument expression
            struct tag_AST_NODE* Right;  // Right operand for binary operations
            BOOL IsShellCommand;
            LPSTR CommandLine;
        } Expression;
    } Data;
    struct tag_AST_NODE* Next;
} AST_NODE, *LPAST_NODE;

/************************************************************************/

struct tag_SCRIPT_CONTEXT;
typedef struct tag_SCRIPT_CONTEXT SCRIPT_CONTEXT;
typedef struct tag_SCRIPT_CONTEXT* LPSCRIPT_CONTEXT;

typedef struct {
    LPCSTR Input;
    U32 Position;
    SCRIPT_TOKEN CurrentToken;
    LPSCRIPT_VAR_TABLE Variables;
    LPSCRIPT_CALLBACKS Callbacks;
    LPSCRIPT_SCOPE CurrentScope;
    LPSCRIPT_CONTEXT Context;
} SCRIPT_PARSER, *LPSCRIPT_PARSER;

struct tag_SCRIPT_CONTEXT {
    SCRIPT_VAR_TABLE Variables;
    SCRIPT_CALLBACKS Callbacks;
    LPVOID HeapBase;
    SCRIPT_ERROR ErrorCode;
    STR ErrorMessage[MAX_ERROR_MESSAGE];
    LPSCRIPT_SCOPE GlobalScope;
    LPSCRIPT_SCOPE CurrentScope;
    SCRIPT_HOST_REGISTRY HostRegistry;
};

/************************************************************************/

LPSCRIPT_CONTEXT ScriptCreateContext(LPSCRIPT_CALLBACKS Callbacks);
void ScriptDestroyContext(LPSCRIPT_CONTEXT Context);

SCRIPT_ERROR ScriptExecute(LPSCRIPT_CONTEXT Context, LPCSTR Script);

LPSCRIPT_VARIABLE ScriptSetVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value);
LPSCRIPT_VARIABLE ScriptGetVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name);
void ScriptDeleteVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name);

SCRIPT_ERROR ScriptGetLastError(LPSCRIPT_CONTEXT Context);
LPCSTR ScriptGetErrorMessage(LPSCRIPT_CONTEXT Context);

// Array support functions
LPSCRIPT_ARRAY ScriptCreateArray(U32 InitialCapacity);
void ScriptDestroyArray(LPSCRIPT_ARRAY Array);
SCRIPT_ERROR ScriptArraySet(LPSCRIPT_ARRAY Array, U32 Index, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value);
SCRIPT_ERROR ScriptArrayGet(LPSCRIPT_ARRAY Array, U32 Index, SCRIPT_VAR_TYPE* Type, SCRIPT_VAR_VALUE* Value);
LPSCRIPT_VARIABLE ScriptSetArrayElement(LPSCRIPT_CONTEXT Context, LPCSTR Name, U32 Index, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value);
LPSCRIPT_VARIABLE ScriptGetArrayElement(LPSCRIPT_CONTEXT Context, LPCSTR Name, U32 Index);

// Host object registration
BOOL ScriptRegisterHostSymbol(LPSCRIPT_CONTEXT Context, LPCSTR Name, SCRIPT_HOST_SYMBOL_KIND Kind, SCRIPT_HOST_HANDLE Handle, const SCRIPT_HOST_DESCRIPTOR* Descriptor, LPVOID ContextPointer);
void ScriptUnregisterHostSymbol(LPSCRIPT_CONTEXT Context, LPCSTR Name);
void ScriptClearHostSymbols(LPSCRIPT_CONTEXT Context);

// Scope management functions
LPSCRIPT_SCOPE ScriptCreateScope(LPSCRIPT_SCOPE Parent);
void ScriptDestroyScope(LPSCRIPT_SCOPE Scope);
LPSCRIPT_SCOPE ScriptPushScope(LPSCRIPT_CONTEXT Context);
void ScriptPopScope(LPSCRIPT_CONTEXT Context);
LPSCRIPT_VARIABLE ScriptFindVariableInScope(LPSCRIPT_SCOPE Scope, LPCSTR Name, BOOL SearchParents);
LPSCRIPT_VARIABLE ScriptSetVariableInScope(LPSCRIPT_SCOPE Scope, LPCSTR Name, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value);

// AST management functions
LPAST_NODE ScriptCreateASTNode(AST_NODE_TYPE Type);
void ScriptDestroyAST(LPAST_NODE Node);
SCRIPT_ERROR ScriptExecuteAST(LPSCRIPT_PARSER Parser, LPAST_NODE Node);

/************************************************************************/