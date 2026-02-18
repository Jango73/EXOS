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


    Script Exposure Helpers - Keyboard

\************************************************************************/

#include "Exposed.h"

#include "DriverGetters.h"
#include "KernelData.h"
#include "drivers/KeyboardDrivers.h"

/************************************************************************/

#define EXPOSE_ACCESS_KEYBOARD (EXPOSE_ACCESS_ADMIN | EXPOSE_ACCESS_KERNEL)

/************************************************************************/

/**
 * @brief Retrieves the active keyboard driver.
 * @return Pointer to the active keyboard driver or NULL if none is available.
 */
static LPDRIVER KeyboardGetActiveDriver(void) {
    LPDRIVER UsbKeyboardDriver = USBKeyboardGetDriver();
    if (UsbKeyboardDriver != NULL &&
        (UsbKeyboardDriver->Flags & DRIVER_FLAG_READY) != 0u) {
        return UsbKeyboardDriver;
    }

    LPDRIVER Ps2KeyboardDriver = StdKeyboardGetDriver();
    if (Ps2KeyboardDriver != NULL &&
        (Ps2KeyboardDriver->Flags & DRIVER_FLAG_READY) != 0u) {
        return Ps2KeyboardDriver;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed keyboard object.
 * @param Context Host callback context (unused for keyboard exposure)
 * @param Parent Handle to the keyboard object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR KeyboardGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_STRING("layout", GetKeyboardCode());

    if (STRINGS_EQUAL_NO_CASE(Property, TEXT("driver"))) {
        EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_KEYBOARD, NULL);
        LPDRIVER ActiveDriver = KeyboardGetActiveDriver();
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
 * @brief Retrieves the keyboard script descriptor.
 * @return Pointer to the keyboard script descriptor.
 */
const SCRIPT_HOST_DESCRIPTOR *GetKeyboardDescriptor(void) {
    static const SCRIPT_HOST_DESCRIPTOR KeyboardDescriptor = {
        KeyboardGetProperty,
        NULL,
        NULL,
        NULL
    };

    return &KeyboardDescriptor;
}

/************************************************************************/

/**
 * @brief Retrieves the keyboard root handle for script exposure.
 * @return Host handle for the keyboard root object.
 */
SCRIPT_HOST_HANDLE GetKeyboardRootHandle(void) {
    static int DATA_SECTION KeyboardRootSentinel = 0;

    return &KeyboardRootSentinel;
}

/************************************************************************/
