
// Clock.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#ifndef CLOCK_H_INCLUDED
#define CLOCK_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

void InitializeClock(void);
U32 GetSystemTime(void);
void MilliSecondsToHMS(U32, LPSTR);
BOOL GetLocalTime(LPSYSTEMTIME);

/***************************************************************************/

#endif
