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


    Shell Script Host Exposure Helpers

\************************************************************************/

#pragma once

#include "Base.h"
#include "CoreString.h"
#include "List.h"
#include "Memory.h"
#include "Script.h"

/************************************************************************/

#define EXPOSE_PROPERTY_GUARD() \
    do { \
        if (OutValue == NULL || Parent == NULL || Property == NULL) { \
            return SCRIPT_ERROR_UNDEFINED_VAR; \
        } \
        MemorySet(OutValue, 0, sizeof(SCRIPT_VALUE)); \
    } while (0)

#define EXPOSE_ARRAY_GUARD() \
    do { \
        if (OutValue == NULL || Parent == NULL) { \
            return SCRIPT_ERROR_UNDEFINED_VAR; \
        } \
    } while (0)

#define EXPOSE_BIND_INTEGER(PropertyName, ValueExpr) \
    do { \
        if (STRINGS_EQUAL_NO_CASE(Property, TEXT(PropertyName))) { \
            OutValue->Type = SCRIPT_VAR_INTEGER; \
            OutValue->Value.Integer = (I32)(ValueExpr); \
            return SCRIPT_OK; \
        } \
    } while (0)

#define EXPOSE_BIND_STRING(PropertyName, ValueExpr) \
    do { \
        if (STRINGS_EQUAL_NO_CASE(Property, TEXT(PropertyName))) { \
            OutValue->Type = SCRIPT_VAR_STRING; \
            OutValue->Value.String = (LPSTR)(ValueExpr); \
            OutValue->OwnsValue = FALSE; \
            return SCRIPT_OK; \
        } \
    } while (0)

#define EXPOSE_BIND_HOST_HANDLE(PropertyName, HandleValue, DescriptorValue, ContextValue) \
    do { \
        if (STRINGS_EQUAL_NO_CASE(Property, TEXT(PropertyName))) { \
            OutValue->Type = SCRIPT_VAR_HOST_HANDLE; \
            OutValue->Value.HostHandle = (HandleValue); \
            OutValue->HostDescriptor = (DescriptorValue); \
            OutValue->HostContext = (ContextValue); \
            OutValue->OwnsValue = FALSE; \
            return SCRIPT_OK; \
        } \
    } while (0)

#define EXPOSE_SET_HOST_HANDLE(HandleValue, DescriptorValue, ContextValue, OwnsHandle) \
    do { \
        MemorySet(OutValue, 0, sizeof(SCRIPT_VALUE)); \
        OutValue->Type = SCRIPT_VAR_HOST_HANDLE; \
        OutValue->Value.HostHandle = (HandleValue); \
        OutValue->HostDescriptor = (DescriptorValue); \
        OutValue->HostContext = (ContextValue); \
        OutValue->OwnsValue = (OwnsHandle); \
    } while (0)

#define EXPOSE_LIST_ARRAY_GET_ELEMENT(FunctionName, ItemType, ValidMacro, ValidId, DescriptorValue) \
    SCRIPT_ERROR FunctionName(LPVOID Context, SCRIPT_HOST_HANDLE Parent, U32 Index, LPSCRIPT_VALUE OutValue) { \
        UNUSED(Context); \
        EXPOSE_ARRAY_GUARD(); \
        LPLIST List = (LPLIST)Parent; \
        if (List == NULL || Index >= ListGetSize(List)) { \
            return SCRIPT_ERROR_UNDEFINED_VAR; \
        } \
        ItemType Item = (ItemType)ListGetItem(List, Index); \
        ValidMacro(Item, ValidId) { \
            EXPOSE_SET_HOST_HANDLE(Item, DescriptorValue, NULL, FALSE); \
            return SCRIPT_OK; \
        } \
        return SCRIPT_ERROR_UNDEFINED_VAR; \
    }

/************************************************************************/

SCRIPT_ERROR ProcessGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR ProcessArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR ProcessDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR ProcessArrayDescriptor;

SCRIPT_ERROR UsbGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbPortGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbPortArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbPortArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDeviceGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDeviceArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDeviceArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR UsbDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbPortDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbPortArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbDeviceDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbDeviceArrayDescriptor;
extern SCRIPT_HOST_HANDLE UsbRootHandle;

/************************************************************************/
