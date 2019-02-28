
// ShowIRQ.c

/***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include <graph.h>

/***************************************************************************/

void main ()
{
  unsigned long Mask21;
  unsigned long MaskA1;

  Mask21 = inp(0x21);
  MaskA1 = inp(0xA1);

  fprintf(stdout, "8259-1 mask : %02X\n", Mask21);
  fprintf(stdout, "8259-2 mask : %02X\n", MaskA1);
}

/***************************************************************************/
