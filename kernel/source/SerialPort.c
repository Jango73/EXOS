
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/System.h"

/***************************************************************************/

const U2 COMPorts[4] = { 0x3F8 , 0x2F8, 0x3E8, 0x2E8 };

/***************************************************************************/

void SerialReset(U8 Which) {
    if (Which > 3) return;

    OutPortByte(COMPorts[Which] + 1, 0x00);  // Deactivate interrupts
    OutPortByte(COMPorts[Which] + 3, 0x80);  // Activate DLAB
    OutPortByte(COMPorts[Which] + 0, 0x03);  // Baud rate 38400
    OutPortByte(COMPorts[Which] + 1, 0x00);  // High divisor
    OutPortByte(COMPorts[Which] + 3, 0x03);  // 8 bits, no parity, 1 stop bit
    OutPortByte(COMPorts[Which] + 2, 0xC7);  // Activate FIFO
    OutPortByte(COMPorts[Which] + 4, 0x0B);  // Activate interrupts, RTS/DSR
}

/***************************************************************************/

void SerialOut(U8 Which, STR Char) {
    if (Which > 3) return;

    while (!(InPortByte(COMPorts[Which] + 5) & 0x20))
        ;
    OutPortByte(COMPorts[Which], Char);
}
