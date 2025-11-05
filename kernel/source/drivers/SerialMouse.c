
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


    Serial Mouse

\************************************************************************/

#include "Base.h"
#include "GFX.h"
#include "InterruptController.h"
#include "Log.h"
#include "Mouse.h"
#include "process/Process.h"
#include "CoreString.h"
#include "User.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

UINT SerialMouseCommands(UINT, UINT);

DRIVER SerialMouseDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_MOUSE,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Not applicable",
    .Product = "Standard Serial Mouse",
    .Command = SerialMouseCommands};

/***************************************************************************/

typedef struct tag_MOUSEDATA {
    MUTEX Mutex;
    U32 Busy;
    I32 DeltaX;
    I32 DeltaY;
    U32 Buttons;
    I32 PosX;
    I32 PosY;
} MOUSEDATA, *LPMOUSEDATA;

static MOUSEDATA Mouse = {
    .Mutex = EMPTY_MUTEX, .Busy = 0, .DeltaX = 0, .DeltaY = 0, .Buttons = 0, .PosX = 1, .PosY = 1};

/***************************************************************************/

#define LOGIMOUSE_DATA 0x023C
#define LOGIMOUSE_SIGNATURE 0x023D
#define LOGIMOUSE_CONTROL 0x023E
#define LOGIMOUSE_INTERRUPT 0x023E
#define LOGIMOUSE_CONFIG 0x023F

#define LOGIMOUSE_CONFIG_BYTE 0x91
#define LOGIMOUSE_DEFAULT_MODE 0x90
#define LOGIMOUSE_SIGNATURE_BYTE 0xA5

/***************************************************************************/

#define SERIAL_DATA 0x00
#define SERIAL_INTR 0x01
#define SERIAL_IID 0x02
#define SERIAL_FIFO 0x02
#define SERIAL_LCR 0x03
#define SERIAL_MCR 0x04
#define SERIAL_LSR 0x05
#define SERIAL_MSR 0x06
#define SERIAL_SCRATCH 0x07

#define SERIAL_INTR_R 0x01   // Receive Data Ready
#define SERIAL_INTR_T 0x02   // Transmit Data Empty
#define SERIAL_INTR_LS 0x04  // Line Status
#define SERIAL_INTR_MS 0x08  // Modem Status

// Interrupt ID Register (Read only)

#define SERIAL_IID_I 0x01     // Interrupt Pending
#define SERIAL_IID_ID 0x06    // Cause Mask
#define SERIAL_IID_TD 0x02    // Transmit Data Interrupt
#define SERIAL_IID_RD 0x04    // Receive Data Interrupt
#define SERIAL_IID_FT 0x08    // FIFO Timeout
#define SERIAL_IID_FIFO 0xC0  // Mode Status

// FIFO (First in, first out) Control Register (Write only)

#define SERIAL_FIFO_FE 0x01   // FIFO Enable
#define SERIAL_FIFO_RR 0x02   // Receive Buffer Reset
#define SERIAL_FIFO_TR 0x04   // Transmit Buffer Reset
#define SERIAL_FIFO_FTS 0xC0  // FIFO Trigger Size

// Line Control Register

#define SERIAL_LCR_WS 0x03    // Word Size Mask
#define SERIAL_LCR_WS_5 0x00  // Word size - 5 bits
#define SERIAL_LCR_WS_6 0x01  // Word size - 6 bits
#define SERIAL_LCR_WS_7 0x02  // Word size - 7 bits
#define SERIAL_LCR_WS_8 0x03  // Word size - 8 bits
#define SERIAL_LCR_S 0x04     // Stop bits - 1 or 2
#define SERIAL_LCR_P 0x08     // Parity Enable
#define SERIAL_LCR_EP 0x10    // Even Parity - 1 = even
#define SERIAL_LCR_SP 0x20    // Sticky Parity
#define SERIAL_LCR_B 0x40     // Send Break
#define SERIAL_LCR_D 0x80     // Access Divisor Latch

// Modem Control Register

#define SERIAL_MCR_DTR 0x01  // Data Terminal Ready
#define SERIAL_MCR_RTS 0x02  // Request To Send
#define SERIAL_MCR_O1 0x04   // Out 1
#define SERIAL_MCR_O2 0x08   // Out 2 - Master Enable Interrupts
#define SERIAL_MCR_L 0x10    // Loop

// Line Status Register

#define SERIAL_LSR_DR 0x01  // Data In Receive Buffer
#define SERIAL_LSR_OE 0x02  // Overrun Error
#define SERIAL_LSR_PE 0x04  // Parity Error
#define SERIAL_LSR_FE 0x08  // Framing Error
#define SERIAL_LSR_BI 0x10  // Break Interrupt
#define SERIAL_LSR_TH 0x20  // Transmitter holding register empty
#define SERIAL_LSR_TS 0x40  // Transmitter shift register empty
#define SERIAL_LSR_RE 0x80  // Error in receive FIFO

// Modem Status Register

#define SERIAL_MSR_DCTS 0x01  // Change in CTS
#define SERIAL_MSR_DDSR 0x02  // Change in DSR
#define SERIAL_MSR_DRI 0x04   // Change in RI
#define SERIAL_MSR_DDCD 0x08  // Change in DCD
#define SERIAL_MSR_CTS 0x10   // Clear To Send
#define SERIAL_MSR_DSR 0x20   // Data Set Ready
#define SERIAL_MSR_RI 0x40    // Ring Indicator
#define SERIAL_MSR_DCD 0x80   // Data Carrier Detect

#define MOUSE_PORT 0x03F8
#define MOUSE_TIMEOUT 0x4000

/***************************************************************************/

static void SendBreak(void) {
    U32 Byte;

    Byte = InPortByte(MOUSE_PORT + SERIAL_LCR);
    Byte |= SERIAL_LCR_B;
    OutPortByte(MOUSE_PORT + SERIAL_LCR, Byte);

    Byte = InPortByte(MOUSE_PORT + SERIAL_LCR);
    Byte &= (~SERIAL_LCR_B);
    OutPortByte(MOUSE_PORT + SERIAL_LCR, Byte);
}

/***************************************************************************/

static void Delay(void) {
    U32 Index, Data;
    for (Index = 0; Index < 100000; Index++) {
        Data = Index;
    }
    UNUSED(Data);
}

/***************************************************************************/

static BOOL WaitMouseData(U32 TimeOut) {
    U32 Status;

    while (TimeOut) {
        Status = InPortByte(MOUSE_PORT + SERIAL_LSR);

        if ((Status & SERIAL_LSR_OE) || (Status & SERIAL_LSR_PE) || (Status & SERIAL_LSR_FE) ||
            (Status & SERIAL_LSR_RE)) {
            SendBreak();
            return FALSE;
        }

        if ((Status & SERIAL_LSR_DR) == SERIAL_LSR_DR) return TRUE;

        TimeOut--;
    }

    return FALSE;
}

/***************************************************************************/

static U32 MouseInitialize(void) {
    U32 Sig1, Sig2;
    U32 Byte, Index;
    
    UNUSED(Sig1);
    UNUSED(Sig2);

    OutPortByte(LOGIMOUSE_CONFIG, 0);

    InitMutex(&(Mouse.Mutex));

    for (Index = 0; Index < 8; Index++) {
        OutPortByte(MOUSE_PORT + Index, 0);
    }

    //-------------------------------------
    // Purge the data port

    Byte = InPortByte(MOUSE_PORT + SERIAL_DATA);
    Delay();
    Byte = InPortByte(MOUSE_PORT + SERIAL_DATA);
    Delay();
    Byte = InPortByte(MOUSE_PORT + SERIAL_DATA);
    Delay();
    Byte = InPortByte(MOUSE_PORT + SERIAL_DATA);
    Delay();
    Byte = InPortByte(MOUSE_PORT + SERIAL_DATA);
    Delay();
    Byte = InPortByte(MOUSE_PORT + SERIAL_DATA);
    Delay();

    //-------------------------------------
    // Send a break

    Byte = InPortByte(MOUSE_PORT + SERIAL_LCR);
    Byte |= SERIAL_LCR_B;

    OutPortByte(MOUSE_PORT + SERIAL_LCR, Byte);

    /*
      //-------------------------------------
      // Clear break

      Byte = InPortByte(MOUSE_PORT + SERIAL_LCR);
      Byte &= (~SERIAL_LCR_B);

      OutPortByte(MOUSE_PORT + SERIAL_LCR, Byte);
    */

    //-------------------------------------
    // Clear DTR and RTS

    Byte = InPortByte(MOUSE_PORT + SERIAL_MCR);
    Byte &= (~(SERIAL_MCR_DTR | SERIAL_MCR_RTS));

    OutPortByte(MOUSE_PORT + SERIAL_MCR, Byte);

    //-------------------------------------
    // Set DTR, RTS and O2

    Byte = InPortByte(MOUSE_PORT + SERIAL_MCR);
    Byte |= SERIAL_MCR_DTR;
    Byte |= SERIAL_MCR_RTS;
    Byte |= SERIAL_MCR_O2;

    OutPortByte(MOUSE_PORT + SERIAL_MCR, Byte);

    //-------------------------------------
    // Check signature of mouse

    WaitMouseData(MOUSE_TIMEOUT);
    Sig1 = InPortByte(MOUSE_PORT + SERIAL_DATA);
    WaitMouseData(MOUSE_TIMEOUT);
    Sig2 = InPortByte(MOUSE_PORT + SERIAL_DATA);

    //-------------------------------------
    // Enable the Receive Data Interrupt

    Byte = InPortByte(MOUSE_PORT + SERIAL_INTR);
    Byte |= SERIAL_INTR_R;

    OutPortByte(MOUSE_PORT + SERIAL_INTR, Byte);

    //-------------------------------------
    // Set word size

    Byte = InPortByte(MOUSE_PORT + SERIAL_LCR);
    Byte &= (~SERIAL_LCR_WS);
    Byte |= SERIAL_LCR_WS_7;

    OutPortByte(MOUSE_PORT + SERIAL_LCR, Byte);

    //-------------------------------------
    // Clear break

    Byte = InPortByte(MOUSE_PORT + SERIAL_LCR);
    Byte &= (~SERIAL_LCR_B);

    OutPortByte(MOUSE_PORT + SERIAL_LCR, Byte);

    //-------------------------------------
    //

    DEBUG(TEXT("[MouseInitialize] Mouse found on COM1: %c%c"), Sig1, Sig2);

    //-------------------------------------
    // Enable the mouse's IRQ

    EnableInterrupt(IRQ_MOUSE);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 GetDeltaX(void) {
    U32 Result = 0;
    LockMutex(&(Mouse.Mutex), INFINITY);
    Result = UNSIGNED(Mouse.DeltaX);
    UnlockMutex(&(Mouse.Mutex));
    return Result;
}

/***************************************************************************/

static U32 GetDeltaY(void) {
    U32 Result = 0;
    LockMutex(&(Mouse.Mutex), INFINITY);
    Result = UNSIGNED(Mouse.DeltaY);
    UnlockMutex(&(Mouse.Mutex));
    return Result;
}

/***************************************************************************/

static U32 GetButtons(void) {
    U32 Result;
    LockMutex(&(Mouse.Mutex), INFINITY);
    Result = UNSIGNED(Mouse.Buttons);
    UnlockMutex(&(Mouse.Mutex));
    return Result;
}

/***************************************************************************/

/*
static void DrawMouseCursor(I32 X, I32 Y) {
    LINEINFO LineInfo;

    LineInfo.GC = 0;

    LineInfo.X1 = X - 4;
    LineInfo.Y1 = Y;
    LineInfo.X2 = X + 4;
    LineInfo.Y2 = Y;
    VESADriver.Command(DF_GFX_LINE, (UINT)&LineInfo);

    LineInfo.X1 = X;
    LineInfo.Y1 = Y - 4;
    LineInfo.X2 = X;
    LineInfo.Y2 = Y + 4;
    VESADriver.Command(DF_GFX_LINE, (UINT)&LineInfo);
}
*/

/***************************************************************************/

/*
static void MouseHandler_Microsoft(void) {
    static U32 Buttons;
    static U32 DeltaX, DeltaY;

    if (WaitMouseData(MOUSE_TIMEOUT) == FALSE) return;

    Buttons = InPortByte(MOUSE_PORT + SERIAL_DATA);

    if ((Buttons & BIT_6) != BIT_6) {
        SendBreak();

        InPortByte(MOUSE_PORT + SERIAL_DATA);
        Delay();
        InPortByte(MOUSE_PORT + SERIAL_DATA);
        Delay();
        InPortByte(MOUSE_PORT + SERIAL_DATA);
        Delay();
        InPortByte(MOUSE_PORT + SERIAL_DATA);
        Delay();

        return;
    }

    if (WaitMouseData(MOUSE_TIMEOUT) == FALSE) return;
    DeltaX = InPortByte(MOUSE_PORT + SERIAL_DATA);

    if (WaitMouseData(MOUSE_TIMEOUT) == FALSE) return;
    DeltaY = InPortByte(MOUSE_PORT + SERIAL_DATA);

    DeltaX = (DeltaX & 0x3F) | ((Buttons & 0x03) << 6);
    DeltaY = (DeltaY & 0x3F) | ((Buttons & 0x0C) << 4);

    Buttons = (Buttons & 0x30) >> 4;

    Mouse.Buttons = 0;

    if (Buttons & 2) Mouse.Buttons |= MB_LEFT;
    if (Buttons & 1) Mouse.Buttons |= MB_RIGHT;

    Mouse.DeltaX += *((I8*)&DeltaX);
    Mouse.DeltaY += *((I8*)&DeltaY);

    // if (Mouse.DeltaX < -4096) Mouse.DeltaX = -4096;
    // if (Mouse.DeltaY < -4096) Mouse.DeltaY = -4096;

    if (Mouse.DeltaX < 0) Mouse.DeltaX = 0;
    if (Mouse.DeltaY < 0) Mouse.DeltaY = 0;
    if (Mouse.DeltaX > 4096) Mouse.DeltaX = 4096;
    if (Mouse.DeltaY > 4096) Mouse.DeltaY = 4096;

    // DrawMouseCursor(Mouse.DeltaX, Mouse.DeltaY);
}
*/

/***************************************************************************/

/*
static void MouseHandler_MouseSystems(void) {
    // static U32 Status;
    static U32 Buttons;
    static U32 DeltaX1, DeltaY1;

    // Status = InPortByte(MOUSE_PORT + SERIAL_IID);
    // if ((Status & SERIAL_IID_I) == 0) goto Out;
    // if ((Status & SERIAL_IID_ID) != SERIAL_IID_RD) goto Out;

    if (WaitMouseData(MOUSE_TIMEOUT) == FALSE) goto Out;

    Buttons = InPortByte(MOUSE_PORT + SERIAL_DATA);

    if ((Buttons & 0xF8) != 0x80) goto Out;

    if (WaitMouseData(MOUSE_TIMEOUT) == FALSE) goto Out;
    DeltaX1 = InPortByte(MOUSE_PORT + SERIAL_DATA);
    if (WaitMouseData(MOUSE_TIMEOUT) == FALSE) goto Out;
    DeltaY1 = InPortByte(MOUSE_PORT + SERIAL_DATA);

    Buttons |= 0xFF;

    Mouse.PosX += *((I8*)&DeltaX1);
    Mouse.PosY += *((I8*)&DeltaY1);

Out:
}
*/

/***************************************************************************/

void MouseHandler(void) {
    DEBUG(TEXT("[MouseHandler]"));

    // MouseHandler_Microsoft();
    // MouseHandler_MouseSystems();
}

/***************************************************************************/

UINT SerialMouseCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            return (UINT)MouseInitialize();
        case DF_GETVERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_MOUSE_RESET:
            return 0;
        case DF_MOUSE_GETDELTAX:
            return (UINT)GetDeltaX();
        case DF_MOUSE_GETDELTAY:
            return (UINT)GetDeltaY();
        case DF_MOUSE_GETBUTTONS:
            return (UINT)GetButtons();
    }

    return (UINT)MAX_U32;
}
