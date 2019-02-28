
// DumpVga.c

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void main ()
{
  char* Base = (char*) 0xC0000;
  char Hexa [64];
  char Text [32];
  char Temp [16];
  char Data;
  int c;

  while (Base < 0xC8000)
  {
    fprintf(stdout, "%08X ", Base);
    Hexa[0] = 0;
    Text[0] = 0;
    for (c = 0; c < 16; c++)
    {
      Data = *Base;
      sprintf(Temp, "%02X ", Data); strcat(Hexa, Temp);
      if (Data >= 0x20) Text[c] = Data; else Text[c] = '.';
      Base++;
    }
    Text[c] = 0;
    fprintf(stdout, "%s%s\n", Hexa, Text);
  }
}
