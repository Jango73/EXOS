/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/SerialPort.h"

#include "../include/System.h"

/************************************************************************/

const U16 COMPorts[4] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};

/************************************************************************/

void SerialReset(U8 Which) {
    if (Which > 3) return;
    U16 base = COMPorts[Which];

    /* Disable UART interrupts */
    OutPortByte(base + UART_IER, 0x00);

    /* Enable DLAB to program baud rate */
    OutPortByte(base + UART_LCR, LCR_DLAB);

    /* Set baud rate divisor (38400) */
    OutPortByte(base + UART_DLL, (U8)(BAUD_DIV_38400 & 0xFF));
    OutPortByte(base + UART_DLM, (U8)((BAUD_DIV_38400 >> 8) & 0xFF));

    /* 8N1, clear DLAB */
    OutPortByte(base + UART_LCR, LCR_8N1);

    /* Enable FIFO, clear RX/TX, set trigger level */
    OutPortByte(base + UART_FCR, (U8)(FCR_ENABLE | FCR_CLR_RX | FCR_CLR_TX | FCR_TRIG_14));

    /* Assert DTR/RTS and enable OUT2 (required for IRQ routing) */
    OutPortByte(base + UART_MCR, (U8)(MCR_DTR | MCR_RTS | MCR_OUT2));
}

/************************************************************************/

/* Bounded busy-wait TX: safe in critical handlers (no locks/allocations). */
void SerialOut(U8 Which, U8 Char) {
    if (Which > 3) return;
    U16 base = COMPorts[Which];

    const U32 MaxSpin = 100000; /* Upper bound to avoid deadlock */
    U32 spins = 0;

    /* Wait for THR empty (LSR_THRE). Give up on timeout. */
    while (!(InPortByte(base + UART_LSR) & LSR_THRE)) {
        if (++spins >= MaxSpin) return;
    }

    OutPortByte(base + UART_THR, Char);
}
