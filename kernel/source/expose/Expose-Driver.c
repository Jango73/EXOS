
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


    Script Exposure Helpers - Driver

\************************************************************************/

#include "Exposed.h"

#include "Driver.h"
#include "KernelData.h"

/************************************************************************/

#define EXPOSE_ACCESS_DRIVER (EXPOSE_ACCESS_ADMIN | EXPOSE_ACCESS_KERNEL)

/************************************************************************/

/**
 * @brief Retrieve a property value from a driver exposed to the script engine.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_DRIVER, NULL);

    LPDRIVER Driver = (LPDRIVER)Parent;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        EXPOSE_BIND_INTEGER("type", Driver->Type);
        EXPOSE_BIND_INTEGER("version_major", Driver->VersionMajor);
        EXPOSE_BIND_INTEGER("version_minor", Driver->VersionMinor);
        EXPOSE_BIND_STRING("designer", Driver->Designer);
        EXPOSE_BIND_STRING("manufacturer", Driver->Manufacturer);
        EXPOSE_BIND_STRING("product", Driver->Product);
        EXPOSE_BIND_INTEGER("flags", Driver->Flags);
        EXPOSE_BIND_INTEGER("enum_domain_count", Driver->EnumDomainCount);

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("enum_domains"))) {
            OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
            OutValue->Value.HostHandle = Driver;
            OutValue->HostDescriptor = &DriverEnumDomainArrayDescriptor;
            OutValue->HostContext = NULL;
            OutValue->OwnsValue = FALSE;
            return SCRIPT_OK;
        }

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed driver array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_DRIVER, NULL);

    LPLIST DriverList = (LPLIST)Parent;
    if (DriverList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(DriverList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a driver from the exposed driver array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting driver handle
 * @return SCRIPT_OK when the driver exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_DRIVER, NULL);

    LPLIST DriverList = (LPLIST)Parent;
    if (DriverList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    if (Index >= ListGetSize(DriverList)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPDRIVER Driver = (LPDRIVER)ListGetItem(DriverList, Index);
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        EXPOSE_SET_HOST_HANDLE(Driver, &DriverDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed driver enum domain array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverEnumDomainArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_DRIVER, NULL);

    LPDRIVER Driver = (LPDRIVER)Parent;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        EXPOSE_BIND_INTEGER("count", Driver->EnumDomainCount);
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve an enum domain from the exposed driver enum domain array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver instance requested by the script
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting enum domain value
 * @return SCRIPT_OK when the domain exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverEnumDomainArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_DRIVER, NULL);

    LPDRIVER Driver = (LPDRIVER)Parent;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        if (Index >= Driver->EnumDomainCount) {
            return SCRIPT_ERROR_UNDEFINED_VAR;
        }

        OutValue->Type = SCRIPT_VAR_INTEGER;
        OutValue->Value.Integer = (I32)Driver->EnumDomains[Index];
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR DriverDescriptor = {
    DriverGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR DriverArrayDescriptor = {
    DriverArrayGetProperty,
    DriverArrayGetElement,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR DriverEnumDomainArrayDescriptor = {
    DriverEnumDomainArrayGetProperty,
    DriverEnumDomainArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
