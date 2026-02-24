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


    Script Engine - Parser Expressions

\************************************************************************/

#include "Base.h"
#include "Heap.h"
#include "List.h"
#include "Log.h"
#include "CoreString.h"
#include "script/Script.h"
#include "script/Script-Internal.h"

/************************************************************************/
/**
 * @brief Initialize a script parser.
 * @param Parser Parser to initialize
 * @param Input Input string to parse
 * @param Variables Variable table
 * @param Callbacks Callback functions
 */
void ScriptInitParser(LPSCRIPT_PARSER Parser, LPCSTR Input, LPSCRIPT_CONTEXT Context) {
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
void ScriptNextToken(LPSCRIPT_PARSER Parser) {
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
            } else if (StringCompare(Parser->CurrentToken.Value, TEXT("return")) == 0) {
                Parser->CurrentToken.Type = TOKEN_RETURN;
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
void ScriptParseStringToken(LPSCRIPT_PARSER Parser, LPCSTR Input, U32* Pos, STR QuoteChar) {
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
LPAST_NODE ScriptParseAssignmentAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
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
LPAST_NODE ScriptParseComparisonAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
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
LPAST_NODE ScriptParseExpressionAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
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
LPAST_NODE ScriptParseTermAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
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
LPAST_NODE ScriptParseFactorAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
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

