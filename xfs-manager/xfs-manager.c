
// XFSMan.c

/*************************************************************************************************/

#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "..\FileSys\FileSys.h"

/*************************************************************************************************/

extern void* Sector0;

/*************************************************************************************************/

#define DPMI_INT 0x31
#define PARA_SIZE 0x10
#define DOSMEM_SIZE 32768

U16 DOSMemSelector = 0;
U16 DOSMemSegment = 0;
char* DOSMemPointer = NULL;

/*************************************************************************************************/

typedef struct tag_BIOS_DISK_PARAMS {
    U8 Res1;
    U8 HeadDown;
    U8 EngineTime;
    U8 BytesPerSector;
    U8 LastSectorNumber;
    U8 SectorGap;
    U8 DataLength;
} BIOS_DISK_PARAMS, *pBIOS_DISK_PARAMS;

/*************************************************************************************************/

char* TitleText =
    "EXOS File System Manager V1.0\n"
    "Copyright (c) 1999 Exelsius, Inc.\n\n";

char* UsageText =
    "Usage :\n"
    "  XFSMan [command | options] \n"
    "\n"
    "Commands : \n"
    "  /fnnn  : Format using nnn bytes per cluster\n"
    "  /i     : Display drive information\n\n";

/*************************************************************************************************/

U32 CurrentDrive = 0;

/*************************************************************************************************/

int ResetDrive(U32 Drive) {
    union REGS Regs;

    Regs.w.ax = 0;
    Regs.h.dl = Drive;
    int386(0x13, &Regs, &Regs);

    return 1;
}

/*************************************************************************************************/

int GetDriveInfo(U32 Drive, pDeviceControlBlock Control) {
    BIOS_DISK_PARAMS* Params = NULL;
    union REGS Regs;
    struct SREGS SRegs;

    ResetDrive(Drive);

    // Get drive type
    Regs.h.ah = 0x15;
    Regs.h.dl = Drive;
    int386(0x13, &Regs, &Regs);

    if (Regs.w.cflag & 0x1) return 0;

    switch (Regs.h.ah) {
        case 0x01:
        case 0x02: {
            Regs.h.ah = 0x08;
            Regs.h.dl = Drive;
            int386x(0x13, &Regs, &Regs, &SRegs);

            if (Regs.w.cflag & 0x1) return 0;

            Params = (BIOS_DISK_PARAMS*)((SRegs.es << 4) + Regs.w.bx);

            Control->Device.DeviceType = Regs.h.bl;
            Control->Device.Cylinders = Regs.h.ch;
            Control->Device.Heads = Regs.h.dh;
            Control->Device.Sectors = Regs.h.cl;

            switch (Params->BytesPerSector) {
                case 0:
                    Control->Device.BytesPerSector = 128;
                case 1:
                    Control->Device.BytesPerSector = 256;
                case 2:
                    Control->Device.BytesPerSector = 512;
                case 3:
                    Control->Device.BytesPerSector = 1024;
            }
        } break;
        case 0x03: {
            Control->Device.Sectors = Regs.w.cx;

            Regs.h.ah = 0x08;
            Regs.h.dl = Drive;
            int386x(0x13, &Regs, &Regs, &SRegs);

            if (Regs.w.cflag & 0x1) return 0;

            Control->Device.DeviceType = Regs.h.bl;
            Control->Device.Cylinders = Regs.h.ch;
            Control->Device.Heads = Regs.h.dh;
        } break;
        default:
            return 0;
    }

    // Control->Device.Sectors    = (Regs.h.cl & 0x3F);

    return 1;
}

/*************************************************************************************************/

int WriteBootSector(pBlockDevice Device, void* Buffer) {
    union REGS Regs;

    if (Buffer == NULL) return 0;

    Regs.h.ah = 0x03;
    Regs.h.al = 1;  // Num sectors
    Regs.h.ch = 0;  // Cylinder
    Regs.h.cl = 1;  // Sector
    Regs.h.dh = 0;  // Head
    Regs.h.dl = 0;  // Drive

    Regs.w.bx = (unsigned)Buffer;

    return 1;
}

/*************************************************************************************************/

int DumpDeviceInfo(pBlockDevice Device) {
    char Temp[32];

    fprintf(stdout, "Device type      : %u\n", Device->DeviceType);
    fprintf(stdout, "Cylinders        : %u\n", Device->Cylinders);
    fprintf(stdout, "Heads            : %u\n", Device->Heads);
    fprintf(stdout, "Sectors          : %u\n", Device->Sectors);
    fprintf(stdout, "Bytes per Sector : %u\n", Device->BytesPerSector);

    return 1;
}

/*************************************************************************************************/

int Initialize() {
    union REGS Regs;

    Regs.w.ax = 0x0100;
    Regs.w.bx = DOSMEM_SIZE / PARA_SIZE;
    int386(DPMI_INT, &Regs, &Regs);

    if (Regs.w.cflag & 0x1) return 0;

    DOSMemSegment = Regs.w.ax;
    DOSMemSelector = Regs.w.dx;
    DOSMemPointer = NULL;

    if (DOSMemSegment == 0) return 0;
    if (DOSMemSelector == 0) return 0;

    return 1;
}

/*************************************************************************************************/

int Deinitialize() {
    union REGS Regs;

    Regs.w.ax = 0x0101;
    Regs.w.dx = DOSMemSelector;
    int386(DPMI_INT, &Regs, &Regs);

    return 1;
}

/*************************************************************************************************/

int main(int argc, char** argv) {
    DeviceControlBlock Control;

    if (Initialize() == 0) return 1;

    if (GetDriveInfo(0, &Control)) {
        DumpDeviceInfo(&(Control.Device));

        WriteBootSector(&(Control.Device), Sector0);
    }

    Deinitialize();

    return 0;
}

/*************************************************************************************************/
