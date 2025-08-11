
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/System.h"

/***************************************************************************/

void SerialReset() {
    OutPortByte(0x3F8 + 1, 0x00);  // Deactivate interrupts
    OutPortByte(0x3F8 + 3, 0x80);  // Activate DLAB
    OutPortByte(0x3F8 + 0, 0x03);  // Baud rate 38400
    OutPortByte(0x3F8 + 1, 0x00);  // High divisor
    OutPortByte(0x3F8 + 3, 0x03);  // 8 bits, no parity, 1 stop bit
    OutPortByte(0x3F8 + 2, 0xC7);  // Activate FIFO
    OutPortByte(0x3F8 + 4, 0x0B);  // Activate interrupts, RTS/DSR
}

/***************************************************************************/

void SerialOut(STR Char) {
    while (!(InPortByte(0x3F8 + 5) & 0x20))
        ;
    OutPortByte(0x3F8, Char);
}
