
// ABSREAD.C

/***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include <graph.h>

/***************************************************************************/

#define HD_PORT_0 0x01F0
#define HD_PORT_1 0x0170

#define HD_DATA         0x00
#define HD_ERROR        0x01
#define HD_NUMSECTORS   0x02
#define HD_SECTOR       0x03
#define HD_CYLINDERLOW  0x04
#define HD_CYLINDERHIGH 0x05
#define HD_HEAD         0x06
#define HD_STATUS       0x07
#define HD_COMMAND      HD_STATUS

#define HD_STATUS_ERROR  0x01
#define HD_STATUS_INDEX  0x02
#define HD_STATUS_ECC    0x04
#define HD_STATUS_DRQ    0x08
#define HD_STATUS_SEEK   0x10
#define HD_STATUS_WERROR 0x20
#define HD_STATUS_READY  0x40
#define HD_STATUS_BUSY   0x80

#define HD_COMMAND_RESTORE     0x10
#define HD_COMMAND_READ        0x20
#define HD_COMMAND_WRITE       0x30
#define HD_COMMAND_VERIFY      0x40
#define HD_COMMAND_FORMAT      0x50
#define HD_COMMAND_INIT        0x60
#define HD_COMMAND_SEEK        0x70
#define HD_COMMAND_DIAGNOSE    0x90
#define HD_COMMAND_SPECIFY     0x91
#define HD_COMMAND_SETIDLE1    0xE3
#define HD_COMMAND_SETIDLE2    0x97

#define HD_PORT HD_PORT_0

#define TIMEOUT 100000

/***************************************************************************/

char Buffer [512];

/***************************************************************************/

int WaitNotBusy (int TimeOut)
{
  unsigned Status;

  while (TimeOut)
  {
    Status = inp(HD_PORT + HD_STATUS);
    if (Status & HD_STATUS_BUSY) continue;
    if (Status & HD_STATUS_READY) return 1;
    TimeOut--;
  }

  fprintf(stdout, "Time out !\n");
  return 0;
}

/***************************************************************************/

int Read
(
  unsigned int Drive,
  unsigned int Cylinder,
  unsigned int Head,
  unsigned int Sector,
  unsigned int NumSectors
)
{
  int c;
  unsigned short Data;

  if (WaitNotBusy(TIMEOUT) == 0) return 0;

  outp(HD_PORT + HD_CYLINDERLOW, Cylinder & 0xFF);
  outp(HD_PORT + HD_CYLINDERHIGH, (Cylinder >> 8) & 0xFF);
  outp(HD_PORT + HD_HEAD, 0xA0 | ((Drive & 0x01) << 4) | (Head & 0x0F));
  outp(HD_PORT + HD_SECTOR, Sector);
  outp(HD_PORT + HD_NUMSECTORS, NumSectors);
  outp(HD_PORT + HD_COMMAND, HD_COMMAND_READ);

  for (c = 0; c < 512;)
  {
    if (WaitNotBusy(TIMEOUT) == 0) return 0;

    Data = inpw(HD_PORT + HD_DATA);

    Buffer[c++] = Data & 0xFF;
    Buffer[c++] = (Data >> 8) & 0xFF;
  }

  return 1;
}

/***************************************************************************/

void Dump ()
{
  int x, y, c, d;
  unsigned Data;
  char Addr [16];
  char Hexa [64];
  char Text [64];
  char Temp [16];

  for (y = 0, c = 0; y < 32; y++)
  {
    Hexa[0] = 0; Text[0] = 0;
    sprintf(Addr, "%04X ", c);
    for (x = 0, d = 0; x < 16; x++)
    {
      Data = Buffer[c++];
      sprintf(Temp, "%02X ", Data); strcat(Hexa, Temp);
      if (Data >= ' ') Text[d] = Data; else Text[d] = '.';
      d++;
    }
    Text[d] = 0;
    fprintf(stdout, "%s %s %s\n", Addr, Hexa, Text);
  }
}

/***************************************************************************/

void main (int argc, char** argv)
{
  unsigned Drive    = atol(argv[1]);
  unsigned Cylinder = atol(argv[2]);
  unsigned Head     = atol(argv[3]);
  unsigned Sector   = atol(argv[4]);
  unsigned Count    = atol(argv[5]);
  unsigned Current  = 0;

  for (Current = 0; Current < Count; Current++)
  {
    _disable();
    Read(Drive, Cylinder, Head, Sector + Current, 1);
    _enable();
    Dump();
  }
}

/***************************************************************************/
