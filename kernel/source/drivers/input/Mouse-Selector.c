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


    Mouse selector

\************************************************************************/

#include "input/Mouse.h"
#include "drivers/input/MouseDrivers.h"

/************************************************************************/

#define MOUSE_SELECTOR_VER_MAJOR 1
#define MOUSE_SELECTOR_VER_MINOR 0

/************************************************************************/

static UINT MouseSelectorCommands(UINT Function, UINT Parameter);

/************************************************************************/

static DRIVER DATA_SECTION MouseSelectorDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_MOUSE,
    .VersionMajor = MOUSE_SELECTOR_VER_MAJOR,
    .VersionMinor = MOUSE_SELECTOR_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "Mouse selector",
    .Alias = "mouse_selector",
    .Flags = 0,
    .Command = MouseSelectorCommands
};

/************************************************************************/

/**
 * @brief Return the active mouse driver.
 * @return Selected mouse driver, or NULL if none is available.
 */
static LPDRIVER MouseSelectorGetActiveDriver(void) {
    LPDRIVER UsbDriver = USBMouseGetDriver();
    LPDRIVER SerialDriver = SerialMouseGetDriver();

    if (UsbDriver != NULL && UsbDriver->Command != NULL && UsbDriver->Command(DF_MOUSE_HAS_DEVICE, 0) == 1U) {
        return UsbDriver;
    }

    if (SerialDriver != NULL && SerialDriver->Command != NULL &&
        (SerialDriver->Flags & DRIVER_FLAG_READY) != 0) {
        return SerialDriver;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Load the selector and ensure mouse backends are initialized.
 * @param Parameter Unused.
 * @return Driver status code.
 */
static UINT MouseSelectorLoad(UINT Parameter) {
    UINT Result = DF_RETURN_UNEXPECTED;

    UNUSED(Parameter);

    if ((MouseSelectorDriver.Flags & DRIVER_FLAG_READY) != 0) {
        return DF_RETURN_SUCCESS;
    }

    Result = USBMouseGetDriver()->Command(DF_LOAD, 0);
    if (Result != DF_RETURN_SUCCESS && Result != DF_RETURN_UNEXPECTED) {
        return Result;
    }

    Result = SerialMouseGetDriver()->Command(DF_LOAD, 0);
    if (Result != DF_RETURN_SUCCESS && Result != DF_RETURN_UNEXPECTED) {
        return Result;
    }

    MouseSelectorDriver.Flags |= DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unload the selector and all mouse backends.
 * @param Parameter Unused.
 * @return Driver status code.
 */
static UINT MouseSelectorUnload(UINT Parameter) {
    UNUSED(Parameter);

    (void)USBMouseGetDriver()->Command(DF_UNLOAD, 0);
    (void)SerialMouseGetDriver()->Command(DF_UNLOAD, 0);
    MouseSelectorDriver.Flags &= ~DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Forward one command to the active mouse driver.
 * @param Function Driver command.
 * @param Parameter Command parameter.
 * @return Driver result.
 */
static UINT MouseSelectorForward(UINT Function, UINT Parameter) {
    LPDRIVER Driver = MouseSelectorGetActiveDriver();

    if (Driver == NULL || Driver->Command == NULL) {
        if (Function == DF_MOUSE_HAS_DEVICE) {
            return 0;
        }
        return DF_RETURN_UNEXPECTED;
    }

    return Driver->Command(Function, Parameter);
}

/************************************************************************/

/**
 * @brief Selector driver entry point.
 * @param Function Driver command.
 * @param Parameter Command parameter.
 * @return Driver result.
 */
static UINT MouseSelectorCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return MouseSelectorLoad(Parameter);
        case DF_UNLOAD:
            return MouseSelectorUnload(Parameter);
        case DF_GET_VERSION:
            return MAKE_VERSION(MOUSE_SELECTOR_VER_MAJOR, MOUSE_SELECTOR_VER_MINOR);
        case DF_DEBUG_INFO:
        case DF_MOUSE_RESET:
        case DF_MOUSE_GETDELTAX:
        case DF_MOUSE_GETDELTAY:
        case DF_MOUSE_GETBUTTONS:
        case DF_MOUSE_HAS_DEVICE:
            return MouseSelectorForward(Function, Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Retrieve the mouse selector driver descriptor.
 * @return Pointer to the selector driver.
 */
LPDRIVER MouseSelectorGetDriver(void) {
    return &MouseSelectorDriver;
}

/************************************************************************/
