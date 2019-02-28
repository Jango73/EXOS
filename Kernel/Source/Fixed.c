
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void main ()
{
  long a = -20;
  long b = 1;
  long c = 2;
  long d, e;
  short t;

  t = -20;

  b <<= 8;
  c <<= 8;

  fprintf(stdout, "b=%08X, c=%08X\n", b, c);

  d = (b<<8)/c;

  fprintf(stdout, "d=%08X\n", d);

  e = (t*d);

  fprintf(stdout, "e=%08X, e>>8=%d\n", e, (short) e>>8);
}

