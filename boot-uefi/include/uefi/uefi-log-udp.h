/************************************************************************\

    EXOS UEFI UDP logger

\************************************************************************/

#ifndef UEFI_LOG_UDP_H_INCLUDED
#define UEFI_LOG_UDP_H_INCLUDED

/************************************************************************/

#include "uefi/efi.h"

/************************************************************************/

void BootUefiUdpLogInitialize(EFI_BOOT_SERVICES* BootServices);
void BootUefiUdpLogNotifyExitBootServices(void);
void BootUefiUdpLogWrite(LPCSTR Text);
U32 BootUefiUdpLogGetInitFlags(void);

#define UEFI_UDP_INIT_FLAG_LOCATE_OK      0x1
#define UEFI_UDP_INIT_FLAG_START_OK       0x2
#define UEFI_UDP_INIT_FLAG_INITIALIZE_OK  0x4
#define UEFI_UDP_INIT_FLAG_ENABLED        0x8

/************************************************************************/

#endif  // UEFI_LOG_UDP_H_INCLUDED
