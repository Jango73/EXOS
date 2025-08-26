
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef SERIAL_PORT_H_INCLUDED
#define SERIAL_PORT_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/
// UART 16550A

/* Register offsets (from base I/O port) */
#define UART_RBR 0x00 /* Receiver Buffer Register   (read)  */
#define UART_THR 0x00 /* Transmit Holding Register  (write) */
#define UART_DLL 0x00 /* Divisor Latch LSB (when DLAB=1)    */
#define UART_IER 0x01 /* Interrupt Enable Register          */
#define UART_DLM 0x01 /* Divisor Latch MSB (when DLAB=1)    */
#define UART_IIR 0x02 /* Interrupt Identification (read)    */
#define UART_FCR 0x02 /* FIFO Control Register (write)      */
#define UART_LCR 0x03 /* Line Control Register              */
#define UART_MCR 0x04 /* Modem Control Register             */
#define UART_LSR 0x05 /* Line Status Register               */
#define UART_MSR 0x06 /* Modem Status Register              */
#define UART_SCR 0x07 /* Scratch Register                   */

/* LCR bits */
#define LCR_DLAB 0x80 /* Divisor Latch Access Bit           */
#define LCR_8N1 0x03  /* 8 data bits, No parity, 1 stop     */

/* FCR bits */
#define FCR_ENABLE 0x01  /* FIFO enable                        */
#define FCR_CLR_RX 0x02  /* Clear RX FIFO                      */
#define FCR_CLR_TX 0x04  /* Clear TX FIFO                      */
#define FCR_TRIG_14 0xC0 /* RX trigger level = 14 bytes        */

/* MCR bits */
#define MCR_DTR 0x01
#define MCR_RTS 0x02
#define MCR_OUT2 0x08 /* Required to gate IRQ to PIC        */

/* LSR bits */
#define LSR_THRE 0x20 /* Transmit Holding Register Empty    */

/* Baud rate divisor for 38400 @ 115200 base clock */
#define BAUD_DIV_38400 0x0003

/***************************************************************************/

void SerialReset(U8 Which);  // 0 = COM1, 1 = COM2, ...
void SerialOut(U8 Which, STR Char);

/***************************************************************************/

#endif
