
// Log.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Kernel.h"

/***************************************************************************/

void KernelLogText (U32 Type, LPSTR Text)
{
  switch (Type)
  {
    case LOG_DEBUG :
    {
      #ifdef __DEBUG__
      KernelPrint("DEBUG: ");
      KernelPrint(Text);
      KernelPrint(Text_NewLine);
      #endif
    }
    break;

    case LOG_VERBOSE :
    {
      KernelPrint("VERBOSE: ");
      KernelPrint(Text);
      KernelPrint(Text_NewLine);
    }
    break;

    case LOG_WARNING :
    {
      KernelPrint("WARNING: ");
      KernelPrint(Text);
      KernelPrint(Text_NewLine);
    }
    break;
    
    default :
    {
      KernelPrint(Text);
      KernelPrint(Text_NewLine);
    }
    break;
  }

}

/***************************************************************************/
