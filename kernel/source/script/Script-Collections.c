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


    Script Engine - Values, Arrays and Host Symbols

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
 * @brief Free a variable and its resources.
 * @param Variable Variable to free
 */
void ScriptFreeVariable(LPSCRIPT_VARIABLE Variable) {
    if (Variable == NULL) return;

    if (Variable->Type == SCRIPT_VAR_STRING && Variable->Value.String) {
        HeapFree(Variable->Value.String);
    } else if (Variable->Type == SCRIPT_VAR_ARRAY && Variable->Value.Array) {
        ScriptDestroyArray(Variable->Value.Array);
    }

    HeapFree(Variable);
}

/************************************************************************/

void ScriptValueInit(SCRIPT_VALUE* Value) {
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

void ScriptValueRelease(SCRIPT_VALUE* Value) {
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

U32 ScriptHashHostSymbol(LPCSTR Name) {
    return ScriptHashVariable(Name);
}

/************************************************************************/

BOOL ScriptInitHostRegistry(LPSCRIPT_HOST_REGISTRY Registry) {
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

void ScriptReleaseHostSymbol(LPSCRIPT_HOST_SYMBOL Symbol) {
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

void ScriptClearHostRegistryInternal(LPSCRIPT_HOST_REGISTRY Registry) {
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

LPSCRIPT_HOST_SYMBOL ScriptFindHostSymbol(LPSCRIPT_HOST_REGISTRY Registry, LPCSTR Name) {
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

SCRIPT_ERROR ScriptPrepareHostValue(SCRIPT_VALUE* Value, const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor, LPVOID DefaultContext) {
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

BOOL ScriptValueToFloat(const SCRIPT_VALUE* Value, F32* OutValue) {
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
 * @brief Concatenate two script strings and store the result.
 * @param LeftValue Left operand (must be a string)
 * @param RightValue Right operand (must be a string)
 * @param Result Destination value
 * @return SCRIPT_OK on success, otherwise an error code
 */
SCRIPT_ERROR ScriptConcatStrings(const SCRIPT_VALUE* LeftValue, const SCRIPT_VALUE* RightValue, SCRIPT_VALUE* Result) {
    if (LeftValue == NULL || RightValue == NULL || Result == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (LeftValue->Type != SCRIPT_VAR_STRING || RightValue->Type != SCRIPT_VAR_STRING) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    LPCSTR LeftText = LeftValue->Value.String ? LeftValue->Value.String : TEXT("");
    LPCSTR RightText = RightValue->Value.String ? RightValue->Value.String : TEXT("");

    UINT LeftLength = StringLength(LeftText);
    UINT RightLength = StringLength(RightText);
    UINT TotalLength = LeftLength + RightLength + 1;

    LPSTR NewString = (LPSTR)HeapAlloc(TotalLength);
    if (NewString == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    StringCopy(NewString, LeftText);
    StringConcat(NewString, RightText);

    Result->Type = SCRIPT_VAR_STRING;
    Result->Value.String = NewString;
    Result->OwnsValue = TRUE;

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Remove all occurrences of a substring from a script string.
 * @param LeftValue Source string value
 * @param RightValue Substring value to remove
 * @param Result Destination value
 * @return SCRIPT_OK on success, otherwise an error code
 */
SCRIPT_ERROR ScriptRemoveStringOccurrences(const SCRIPT_VALUE* LeftValue, const SCRIPT_VALUE* RightValue, SCRIPT_VALUE* Result) {
    UINT SourceIndex;
    UINT WriteIndex;
    UINT SourceLength;
    UINT PatternLength;
    LPCSTR SourceText;
    LPCSTR PatternText;
    LPSTR NewString;

    if (LeftValue == NULL || RightValue == NULL || Result == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (LeftValue->Type != SCRIPT_VAR_STRING || RightValue->Type != SCRIPT_VAR_STRING) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    SourceText = LeftValue->Value.String ? LeftValue->Value.String : TEXT("");
    PatternText = RightValue->Value.String ? RightValue->Value.String : TEXT("");

    SourceLength = StringLength(SourceText);
    PatternLength = StringLength(PatternText);

    NewString = (LPSTR)HeapAlloc(SourceLength + 1);
    if (NewString == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    if (PatternLength == 0) {
        StringCopy(NewString, SourceText);
        Result->Type = SCRIPT_VAR_STRING;
        Result->Value.String = NewString;
        Result->OwnsValue = TRUE;
        return SCRIPT_OK;
    }

    SourceIndex = 0;
    WriteIndex = 0;

    while (SourceIndex < SourceLength) {
        if (SourceIndex + PatternLength <= SourceLength &&
            MemoryCompare(SourceText + SourceIndex, PatternText, PatternLength) == 0) {
            SourceIndex += PatternLength;
            continue;
        }

        NewString[WriteIndex++] = SourceText[SourceIndex++];
    }

    NewString[WriteIndex] = STR_NULL;

    Result->Type = SCRIPT_VAR_STRING;
    Result->Value.String = NewString;
    Result->OwnsValue = TRUE;

    return SCRIPT_OK;
}

/************************************************************************/
