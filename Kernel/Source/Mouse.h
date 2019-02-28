
// Mouse.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#ifndef MOUSE_H_INCLUDED
#define MOUSE_H_INCLUDED

/***************************************************************************/

#include "Driver.h"

/***************************************************************************/

#pragma pack (1)

/***************************************************************************/

// Functions supplied by a mouse driver

#define DF_MOUSE_RESET      (DF_FIRSTFUNC + 0)
#define DF_MOUSE_GETDELTAX  (DF_FIRSTFUNC + 1)
#define DF_MOUSE_GETDELTAY  (DF_FIRSTFUNC + 2)
#define DF_MOUSE_GETBUTTONS (DF_FIRSTFUNC + 3)

/***************************************************************************/

#endif
