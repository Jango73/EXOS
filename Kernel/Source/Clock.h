
// Clock.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#ifndef CLOCK_H_INCLUDED
#define CLOCK_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

void InitializeClock   ();
U32  GetSystemTime     ();
void MilliSecondsToHMS (U32, LPSTR);
BOOL GetLocalTime      (LPSYSTEMTIME);

/***************************************************************************/

#endif
