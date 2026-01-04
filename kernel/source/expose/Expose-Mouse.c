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


    Script Exposure Helpers - Mouse

\************************************************************************/

#include "Exposed.h"

#include "Driver.h"
#include "KernelData.h"
#include "MouseDispatcher.h"

/************************************************************************/

#define EXPOSE_ACCESS_MOUSE (EXPOSE_ACCESS_ADMIN | EXPOSE_ACCESS_KERNEL)

/************************************************************************/

/**
 * @brief Retrieves the active mouse driver.
 * @return Pointer to the active mouse driver.
 */
static LPDRIVER MouseGetActiveDriver(void) {
    return GetMouseDriver();
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed mouse object.
 * @param Context Host callback context (unused for mouse exposure)
 * @param Parent Handle to the mouse object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR MouseGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    if (STRINGS_EQUAL_NO_CASE(Property, TEXT("x")) ||
        STRINGS_EQUAL_NO_CASE(Property, TEXT("y"))) {
        I32 MouseX = 0;
        I32 MouseY = 0;

        if (!GetMousePosition(&MouseX, &MouseY)) {
            return SCRIPT_ERROR_UNDEFINED_VAR;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("x"))) {
            OutValue->Type = SCRIPT_VAR_INTEGER;
            OutValue->Value.Integer = MouseX;
            return SCRIPT_OK;
        }

        OutValue->Type = SCRIPT_VAR_INTEGER;
        OutValue->Value.Integer = MouseY;
        return SCRIPT_OK;
    }

    if (STRINGS_EQUAL_NO_CASE(Property, TEXT("driver"))) {
        EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_MOUSE, NULL);
        LPDRIVER ActiveDriver = MouseGetActiveDriver();
        SAFE_USE_VALID_ID(ActiveDriver, KOID_DRIVER) {
            EXPOSE_SET_HOST_HANDLE(ActiveDriver, &DriverDescriptor, NULL, FALSE);
            return SCRIPT_OK;
        }

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieves the mouse script descriptor.
 * @return Pointer to the mouse script descriptor.
 */
const SCRIPT_HOST_DESCRIPTOR *GetMouseDescriptor(void) {
    static const SCRIPT_HOST_DESCRIPTOR MouseDescriptor = {
        MouseGetProperty,
        NULL,
        NULL,
        NULL
    };

    return &MouseDescriptor;
}

/************************************************************************/

/**
 * @brief Retrieves the mouse root handle for script exposure.
 * @return Host handle for the mouse root object.
 */
SCRIPT_HOST_HANDLE GetMouseRootHandle(void) {
    static int MouseRootSentinel = 0;

    return &MouseRootSentinel;
}

/************************************************************************/
