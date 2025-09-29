
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


    Drv Call

\************************************************************************/

#include "../include/Base.h"
#include "../include/Driver.h"
#include "../include/Process.h"

/***************************************************************************/

typedef U32 (*DRVCALLFUNC)(U32);

/***************************************************************************/

/**
 * @brief Default driver function placeholder.
 * @param Parameter Parameter passed to the driver.
 * @return Always returns 0.
 */
U32 DriverFunc(U32 Parameter) {
    UNUSED(Parameter);

    return 0;
}

/***************************************************************************/

#define MAX_DRVCALL 1

DRVCALLFUNC DrvCallTable[MAX_DRVCALL] = {DriverFunc};

/***************************************************************************/

/**
 * @brief Dispatch a driver call from user space.
 * @param Function Index of the driver function to invoke.
 * @param Parameter Parameter passed to the driver function.
 * @return Result returned by the invoked driver function.
 */
U32 DriverCallHandler(U32 Function, U32 Parameter) {
    if (Function < MAX_DRVCALL && DrvCallTable[Function]) {
        return DrvCallTable[Function](Parameter);
    }

    // return ERROR_INVALID_INDEX;
    return 0;
}
