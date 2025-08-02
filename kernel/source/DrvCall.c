
// DrvCall.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "Process.h"

/***************************************************************************/

typedef U32 (*DRVCALLFUNC)(U32);

/***************************************************************************/

U32 DriverFunc(U32 Parameter) { return 0; }

/***************************************************************************/

#define MAX_DRVCALL 1

DRVCALLFUNC DrvCallTable[MAX_DRVCALL] = {DriverFunc};

/***************************************************************************/

U32 DriverCallHandler(U32 Function, U32 Parameter) {
    if (Function < MAX_DRVCALL && DrvCallTable[Function]) {
        return DrvCallTable[Function](Parameter);
    }

    // return ERROR_INVALID_INDEX;
    return 0;
}

/***************************************************************************/
