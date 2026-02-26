
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    E1000

\************************************************************************/

#ifndef E1000_H_INCLUDED
#define E1000_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "Mutex.h" /* Optional: if the driver uses MUTEX internally */
#include "network/Network.h"
#include "drivers/bus/PCI.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
/* Driver-specific command IDs (>= DF_FIRST_FUNCTION)                           */
/* Call via DRIVER::Command for DRIVER_TYPE_NETWORK                        */

#define DF_NET_GETMAC (DF_FIRST_FUNCTION + 0x00)  /* out: U8[6] in param */
#define DF_NET_SEND (DF_FIRST_FUNCTION + 0x01)    /* in: ptr (frame), param2 = len */
#define DF_NET_POLL (DF_FIRST_FUNCTION + 0x02)    /* RX polling */
#define DF_NET_SETRXCB (DF_FIRST_FUNCTION + 0x03) /* in: function pointer to RX callback */

/***************************************************************************/
/* Known PCI IDs (QEMU emulates 82540EM with 0x8086:0x100E)                */

#define E1000_VENDOR_INTEL 0x8086
#define E1000_DEVICE_82540EM 0x100E

/***************************************************************************/
/* MMIO register offsets (subset needed for bring-up)                      */

#define E1000_REG_CTRL 0x0000   /* Device Control */
#define E1000_REG_STATUS 0x0008 /* Device Status */
#define E1000_REG_EERD 0x0014   /* EEPROM Read */
#define E1000_REG_ICR 0x00C0    /* Interrupt Cause Read */
#define E1000_REG_ICS 0x00C8    /* Interrupt Cause Set */
#define E1000_REG_IMS 0x00D0    /* Interrupt Mask Set/Read */
#define E1000_REG_IMC 0x00D8    /* Interrupt Mask Clear */
#define E1000_REG_RCTL 0x0100   /* Receive Control */
#define E1000_REG_TCTL 0x0400   /* Transmit Control */
#define E1000_REG_TIPG 0x0410   /* Transmit Inter-Packet Gap */

#define E1000_REG_RDBAL 0x2800 /* RX Desc Base Address Low */
#define E1000_REG_RDBAH 0x2804 /* RX Desc Base Address High */
#define E1000_REG_RDLEN 0x2808 /* RX Desc Length */
#define E1000_REG_RDH 0x2810   /* RX Desc Head */
#define E1000_REG_RDT 0x2818   /* RX Desc Tail */

#define E1000_REG_TDBAL 0x3800 /* TX Desc Base Address Low */
#define E1000_REG_TDBAH 0x3804 /* TX Desc Base Address High */
#define E1000_REG_TDLEN 0x3808 /* TX Desc Length */
#define E1000_REG_TDH 0x3810   /* TX Desc Head */
#define E1000_REG_TDT 0x3818   /* TX Desc Tail */

/* Optional (MAC table) */
#define E1000_REG_RAL0 0x5400 /* Receive Address Low 0 */
#define E1000_REG_RAH0 0x5404 /* Receive Address High 0 */
#define E1000_REG_MTA  0x5200 /* Multicast Table Array */

/***************************************************************************/
/* CTRL bits                                                               */

#define E1000_CTRL_FD 0x00000001 /* Full duplex */
#define E1000_CTRL_PRIOR 0x00000004
#define E1000_CTRL_SLU 0x00000040 /* Set Link Up */
#define E1000_CTRL_RST 0x04000000 /* Device Reset */

/***************************************************************************/
/* STATUS bits                                                             */

#define E1000_STATUS_FD 0x00000001
#define E1000_STATUS_LU 0x00000002 /* Link Up */

/***************************************************************************/
/* EERD (EEPROM Read)                                                      */
/* Write: [31:24]=0, [23:16]=addr, [0]=start; Read done when bit[4]=1;     */
/* data returned in bits [31:16].                                          */

#define E1000_EERD_START 0x00000001
#define E1000_EERD_DONE 0x00000010
#define E1000_EERD_ADDR_SHIFT 8
#define E1000_EERD_DATA_SHIFT 16

/***************************************************************************/
/* RCTL bits                                                               */

#define E1000_RCTL_EN 0x00000002 /* Receiver Enable */
#define E1000_RCTL_SBP 0x00000004
#define E1000_RCTL_UPE 0x00000008 /* Unicast Promiscuous (debug) */
#define E1000_RCTL_MPE 0x00000010 /* Multicast Promiscuous */
#define E1000_RCTL_LPE 0x00000020
#define E1000_RCTL_RDMTS_HALF 0x00000000
#define E1000_RCTL_BAM 0x00008000 /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2048 0x00000000
#define E1000_RCTL_SECRC 0x04000000 /* Strip Ethernet CRC */

/***************************************************************************/
/* TCTL bits                                                               */

#define E1000_TCTL_EN 0x00000002 /* Transmit Enable */
#define E1000_TCTL_PSP 0x00000008
#define E1000_TCTL_CT_SHIFT 4    /* Collision Threshold */
#define E1000_TCTL_COLD_SHIFT 12 /* Collision Distance */
#define E1000_TCTL_RTLC 0x01000000

/* Recommended defaults (8254x, full-duplex):
   CT (Collision Threshold) = 0x10
   COLD (Collision Distance) = 0x40
   These are written as raw values to be shifted by *_SHIFT. */

#define E1000_TCTL_CT_DEFAULT 0x10
#define E1000_TCTL_COLD_DEFAULT 0x40
#define E1000_TIPG_QEMU_COMPAT 0x00602008

/***************************************************************************/
/* TX descriptor command/status bits                                       */

#define E1000_TX_CMD_EOP 0x01
#define E1000_TX_CMD_IFCS 0x02
#define E1000_TX_CMD_RS 0x08
#define E1000_TX_CMD_DEXT 0x20

#define E1000_TX_STA_DD 0x01

/***************************************************************************/
/* RX descriptor status bits                                               */

#define E1000_RX_STA_DD 0x01   /* Descriptor Done */
#define E1000_RX_STA_EOP 0x02  /* End of Packet */

/***************************************************************************/
/* Descriptor rings & sizes                                                */

#define E1000_RX_DESC_COUNT 128U /* power of two preferred */
#define E1000_TX_DESC_COUNT 128U

/***************************************************************************/
/* Interrupt cause bits                                                    */

#define E1000_INT_TXDW 0x00000001
#define E1000_INT_TXQE 0x00000002
#define E1000_INT_LSC  0x00000004
#define E1000_INT_RXDMT0 0x00000010
#define E1000_INT_RXO  0x00000040
#define E1000_INT_RXT0 0x00000080
#define E1000_DEFAULT_INTERRUPT_MASK (E1000_INT_RXT0 | E1000_INT_RXO | E1000_INT_RXDMT0 | E1000_INT_LSC)
#define E1000_RX_BUF_SIZE 2048U
#define E1000_TX_BUF_SIZE 2048U /* same as RX for consistency */
#define E1000_RING_ALIGN 16U /* descriptor alignment */
#define E1000_PAGE_ALIGN 4096U
#define E1000_ACK_TRACE_LIMIT 16
#define E1000_INTERRUPT_TRACE_LIMIT 32
#define E1000_LINK_SPEED_MBPS 1000
#define E1000_DEFAULT_MTU 1500
#define E1000_TX_TIMEOUT_ITER 100000
#define E1000_RESET_TIMEOUT_ITER 100000

/***************************************************************************/
/* Descriptors                                                             */

/* Receive Descriptor (legacy) */
typedef struct tag_E1000_RXDESC {
    U32 BufferAddrLow;  /* low 32 bits of buffer phys addr */
    U32 BufferAddrHigh; /* high 32 bits (0 on 32-bit) */
    U16 Length;
    U16 Checksum;
    U8 Status;
    U8 Errors;
    U16 Special;
} E1000_RXDESC, *LPE1000_RXDESC;

/* Transmit Descriptor (legacy) */
typedef struct tag_E1000_TXDESC {
    U32 BufferAddrLow;
    U32 BufferAddrHigh;
    U16 Length;
    U8 CSO;
    U8 CMD;
    U8 STA;
    U8 CSS;
    U16 Special;
} E1000_TXDESC, *LPE1000_TXDESC;

/* Sanity sizes (comment only):
   sizeof(E1000_RXDESC) == 16
   sizeof(E1000_TXDESC) == 16
*/

/* Optional: convenience helper to compose the default match table entry.
   (You peux l’ignorer et déclarer E1000Driver directement dans E1000.c) */
#define E1000_MATCH_DEFAULT \
    { E1000_VENDOR_INTEL, E1000_DEVICE_82540EM, PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET, PCI_ANY_CLASS }

extern PCI_DRIVER E1000Driver;
LPDRIVER E1000GetDriver(void);

#pragma pack(pop)

#endif /* E1000_H_INCLUDED */
