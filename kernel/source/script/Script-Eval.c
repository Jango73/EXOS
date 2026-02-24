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


    Script Engine - Evaluation

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
 * @brief Check if a string is a script keyword.
 * @param Str String to check
 * @return TRUE if the string is a keyword
 */
BOOL ScriptIsKeyword(LPCSTR Str) {
    return (StringCompare(Str, TEXT("if")) == 0 ||
            StringCompare(Str, TEXT("else")) == 0 ||
            StringCompare(Str, TEXT("for")) == 0 ||
            StringCompare(Str, TEXT("return")) == 0);
}

/************************************************************************/

/**
 * @brief Evaluate an expression AST node and return its value.
 * @param Parser Parser state (for variable/callback access)
 * @param Expr Expression node
 * @param Error Pointer to error code
 * @return Expression value
 */
SCRIPT_VALUE ScriptEvaluateExpression(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
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

                        if (Status == DF_RETURN_SUCCESS) {
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

            if (Expr->Data.Expression.TokenType == TOKEN_OPERATOR) {
                STR Operator = Expr->Data.Expression.Value[0];
                SCRIPT_ERROR StringError = SCRIPT_OK;

                if (Operator == '+') {
                    if (LeftValue.Type == SCRIPT_VAR_STRING || RightValue.Type == SCRIPT_VAR_STRING) {
                        StringError = ScriptConcatStrings(&LeftValue, &RightValue, &Result);
                        if (StringError != SCRIPT_OK && Error) {
                            *Error = StringError;
                        }
                        ScriptValueRelease(&LeftValue);
                        ScriptValueRelease(&RightValue);
                        return Result;
                    }
                } else if (Operator == '-') {
                    if (LeftValue.Type == SCRIPT_VAR_STRING || RightValue.Type == SCRIPT_VAR_STRING) {
                        StringError = ScriptRemoveStringOccurrences(&LeftValue, &RightValue, &Result);
                        if (StringError != SCRIPT_OK && Error) {
                            *Error = StringError;
                        }
                        ScriptValueRelease(&LeftValue);
                        ScriptValueRelease(&RightValue);
                        return Result;
                    }
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

SCRIPT_VALUE ScriptEvaluateHostProperty(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
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

SCRIPT_VALUE ScriptEvaluateArrayAccess(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
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

