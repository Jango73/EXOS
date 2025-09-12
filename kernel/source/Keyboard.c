
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


    Keyboard

\************************************************************************/

#include "../include/Keyboard.h"

#include "../include/Base.h"
#include "../include/Console.h"
#include "../include/I386.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Process.h"
#include "../include/VKey.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

U32 StdKeyboardCommands(U32, U32);

DRIVER StdKeyboardDriver = {
    .ID = ID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_KEYBOARD,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "IBM PC and compatibles",
    .Product = "Standard IBM PC Keyboard - 102 keys",
    .Command = StdKeyboardCommands};

/***************************************************************************/
// Standard scan codes

#define SCAN_ESCAPE 0x01
#define SCAN_BACK 0x0E
#define SCAN_TAB 0x0F
#define SCAN_ENTER 0x1C
#define SCAN_CONTROL 0x1D
#define SCAN_LEFT_SHIFT 0x2A
#define SCAN_RIGHT_SHIFT 0x36
#define SCAN_START 0x37
#define SCAN_ALT 0x38
#define SCAN_SPACE 0x39
#define SCAN_CAPS_LOCK 0x3A
#define SCAN_F1 0x3B
#define SCAN_F2 0x3C
#define SCAN_F3 0x3D
#define SCAN_F4 0x3E
#define SCAN_F5 0x3F
#define SCAN_F6 0x40
#define SCAN_F7 0x41
#define SCAN_F8 0x42
#define SCAN_F9 0x43
#define SCAN_F10 0x44
#define SCAN_NUM_LOCK 0x45
#define SCAN_SCROLL_LOCK 0x46
#define SCAN_PAD_7 0x47
#define SCAN_PAD_8 0x48
#define SCAN_PAD_9 0x49
#define SCAN_PAD_4 0x4B
#define SCAN_PAD_5 0x4C
#define SCAN_PAD_6 0x4D
#define SCAN_PAD_1 0x4F
#define SCAN_PAD_2 0x50
#define SCAN_PAD_3 0x51
#define SCAN_PAD_0 0x52
#define SCAN_PAD_DOT 0x53
#define SCAN_PAD_MINUS 0x4A
#define SCAN_PAD_PLUS 0x4E
#define SCAN_F11 0x57
#define SCAN_F12 0x58

/***************************************************************************/
// Extended scan codes
// Starting with 0xE0

#define SCAN_PAD_ENTER 0x1C
#define SCAN_RIGHT_CONTROL 0x1D
#define SCAN_PAD_SLASH 0x35
#define SCAN_PAD_STAR 0x37
#define SCAN_RIGHT_ALT 0x38
#define SCAN_HOME 0x47
#define SCAN_UP 0x48
#define SCAN_PAGEUP 0x49
#define SCAN_LEFT 0x4B
#define SCAN_RIGHT 0x4D
#define SCAN_END 0x4F
#define SCAN_DOWN 0x50
#define SCAN_PAGEDOWN 0x51
#define SCAN_INSERT 0x52
#define SCAN_DELETE 0x53

/***************************************************************************/
// Extended scan codes
// Starting with 0xE1 - 0x1D

#define SCAN_PAUSE 0x45

/***************************************************************************/

KEYBOARDSTRUCT Keyboard = {
    .Mutex = EMPTY_MUTEX,
    .Shift = 1,
    .Control = 0,
    .Alt = 0,
    .CapsLock = 0,
    .NumLock = 0,
    .ScrollLock = 0,
    .Pause = 0,
    .Buffer = {{0}},
    .Status = {0}};

static LPKEYTRANS ScanCodeMap = NULL;

/***************************************************************************/

void UseKeyboardLayout(LPCSTR Code) {
    ScanCodeMap = GetScanCodeToKeyCodeTable(Code);
    if (ScanCodeMap == NULL) {
        ScanCodeMap = GetScanCodeToKeyCodeTable(TEXT("en-US"));
    }
}

/***************************************************************************/

static void KeyboardWait(void) {
    U32 Index;

    for (Index = 0; Index < 0x100000; Index++) {
        if ((InPortByte(KEYBOARD_COMMAND) & KSR_IN_FULL) == 0) {
            return;
        }
    }
}

/***************************************************************************/

static BOOL KeyboardACK(void) {
    U32 Loop;

    for (Loop = 0; Loop < 0x100000; Loop++) {
        if (InPortByte(KEYBOARD_COMMAND) & KSR_OUT_FULL) break;
    }

    if (InPortByte(KEYBOARD_DATA) == KSS_ACK) return TRUE;

    return FALSE;
}

/***************************************************************************/

static void SendKeyboardCommand(U32 Command, U32 Data) {
    U32 Flags;

    SaveFlags(&Flags);
    DisableInterrupts();

    KeyboardWait();

    OutPortByte(KEYBOARD_DATA, Command);
    if (KeyboardACK() == FALSE) goto Out;
    OutPortByte(KEYBOARD_DATA, Data);
    if (KeyboardACK() == FALSE) goto Out;

Out:

    RestoreFlags(&Flags);
}

/***************************************************************************/

U16 DetectKeyboard(void) {
    U32 Flags;
    U8 Id1;
    U8 Id2;

    SaveFlags(&Flags);
    DisableInterrupts();
    KeyboardWait();
    OutPortByte(KEYBOARD_DATA, 0xF2);
    KeyboardWait();
    Id1 = InPortByte(KEYBOARD_DATA);
    KeyboardWait();
    Id2 = InPortByte(KEYBOARD_DATA);
    RestoreFlags(&Flags);

    return (U16)(Id2 << 8 | Id1);
}

/***************************************************************************/

static void ScanCodeToKeyCode(U32 ScanCode, LPKEYCODE KeyCode) {
    KeyCode->VirtualKey = 0;
    KeyCode->ASCIICode = 0;

    if (ScanCode >= SCAN_PAD_7 && ScanCode <= SCAN_PAD_DOT) {
        if (Keyboard.NumLock) {
            switch (ScanCode) {
                case SCAN_PAD_7:
                    KeyCode->VirtualKey = VK_7;
                    KeyCode->ASCIICode = '7';
                    break;
                case SCAN_PAD_8:
                    KeyCode->VirtualKey = VK_8;
                    KeyCode->ASCIICode = '8';
                    break;
                case SCAN_PAD_9:
                    KeyCode->VirtualKey = VK_9;
                    KeyCode->ASCIICode = '9';
                    break;
                case SCAN_PAD_4:
                    KeyCode->VirtualKey = VK_4;
                    KeyCode->ASCIICode = '4';
                    break;
                case SCAN_PAD_5:
                    KeyCode->VirtualKey = VK_5;
                    KeyCode->ASCIICode = '5';
                    break;
                case SCAN_PAD_6:
                    KeyCode->VirtualKey = VK_6;
                    KeyCode->ASCIICode = '6';
                    break;
                case SCAN_PAD_1:
                    KeyCode->VirtualKey = VK_1;
                    KeyCode->ASCIICode = '1';
                    break;
                case SCAN_PAD_2:
                    KeyCode->VirtualKey = VK_2;
                    KeyCode->ASCIICode = '2';
                    break;
                case SCAN_PAD_3:
                    KeyCode->VirtualKey = VK_3;
                    KeyCode->ASCIICode = '3';
                    break;
                case SCAN_PAD_0:
                    KeyCode->VirtualKey = VK_0;
                    KeyCode->ASCIICode = '0';
                    break;
                case SCAN_PAD_DOT:
                    KeyCode->VirtualKey = VK_DOT;
                    KeyCode->ASCIICode = '.';
                    break;
            }
        } else {
            switch (ScanCode) {
                case SCAN_PAD_7:
                    KeyCode->VirtualKey = 0;
                    break;
                case SCAN_PAD_8:
                    KeyCode->VirtualKey = VK_UP;
                    break;
                case SCAN_PAD_9:
                    KeyCode->VirtualKey = VK_PAGEUP;
                    break;
                case SCAN_PAD_4:
                    KeyCode->VirtualKey = VK_LEFT;
                    break;
                case SCAN_PAD_5:
                    KeyCode->VirtualKey = 0;
                    break;
                case SCAN_PAD_6:
                    KeyCode->VirtualKey = VK_RIGHT;
                    break;
                case SCAN_PAD_1:
                    KeyCode->VirtualKey = VK_END;
                    break;
                case SCAN_PAD_2:
                    KeyCode->VirtualKey = VK_DOWN;
                    break;
                case SCAN_PAD_3:
                    KeyCode->VirtualKey = VK_PAGEDOWN;
                    break;
                case SCAN_PAD_0:
                    KeyCode->VirtualKey = VK_INSERT;
                    break;
                case SCAN_PAD_DOT:
                    KeyCode->VirtualKey = VK_DELETE;
                    break;
            }
        }
    } else if (Keyboard.Status[SCAN_LEFT_SHIFT] || Keyboard.Status[SCAN_RIGHT_SHIFT] || Keyboard.CapsLock) {
        if (ScanCodeMap) {
            *KeyCode = ScanCodeMap[ScanCode].Shift;
        }
    } else if (Keyboard.Status[SCAN_ALT] || Keyboard.Status[SCAN_RIGHT_ALT]) {
        if (ScanCodeMap) {
            *KeyCode = ScanCodeMap[ScanCode].Alt;
        }
    } else {
        if (ScanCodeMap) {
            *KeyCode = ScanCodeMap[ScanCode].Normal;
        }
    }

    // Echo character
    // *((LPSTR) 0xB8000) = KeyCode->ASCIICode;
}

/***************************************************************************/

static void ScanCodeToKeyCode_E0(U32 ScanCode, LPKEYCODE KeyCode) {
    KeyCode->VirtualKey = 0;
    KeyCode->ASCIICode = 0;

    switch (ScanCode) {
        case SCAN_RIGHT_CONTROL:
            KeyCode->VirtualKey = VK_RCTRL;
            break;
        case SCAN_RIGHT_ALT:
            KeyCode->VirtualKey = VK_RALT;
            break;
        case SCAN_HOME:
            KeyCode->VirtualKey = VK_HOME;
            break;
        case SCAN_UP:
            KeyCode->VirtualKey = VK_UP;
            break;
        case SCAN_PAGEUP:
            KeyCode->VirtualKey = VK_PAGEUP;
            break;
        case SCAN_LEFT:
            KeyCode->VirtualKey = VK_LEFT;
            break;
        case SCAN_RIGHT:
            KeyCode->VirtualKey = VK_RIGHT;
            break;
        case SCAN_END:
            KeyCode->VirtualKey = VK_END;
            break;
        case SCAN_DOWN:
            KeyCode->VirtualKey = VK_DOWN;
            break;
        case SCAN_PAGEDOWN:
            KeyCode->VirtualKey = VK_PAGEDOWN;
            break;
        case SCAN_INSERT:
            KeyCode->VirtualKey = VK_INSERT;
            break;
        case SCAN_DELETE:
            KeyCode->VirtualKey = VK_DELETE;
            break;
        case SCAN_PAD_ENTER: {
            KeyCode->VirtualKey = VK_ENTER;
            KeyCode->ASCIICode = STR_NEWLINE;
        } break;
        case SCAN_PAD_SLASH: {
            KeyCode->VirtualKey = VK_SLASH;
            KeyCode->ASCIICode = STR_SLASH;
        } break;
    }

    // Check reboot sequence

    if (KeyCode->VirtualKey == VK_DELETE) {
        if (Keyboard.Status[SCAN_CONTROL] && Keyboard.Status[SCAN_ALT]) {
            X86REGS Regs;
            RealModeCall(0xF000FFF0, &Regs);
        }
    }
}

/***************************************************************************/

static void ScanCodeToKeyCode_E1(U32 ScanCode, LPKEYCODE KeyCode) {
    KeyCode->VirtualKey = 0;
    KeyCode->ASCIICode = 0;

    switch (ScanCode) {
        case SCAN_PAUSE: {
            KeyCode->VirtualKey = VK_PAUSE;
            KeyCode->ASCIICode = 0;
            /*
              TASKINFO TaskInfo;
              TaskInfo.Func      = Shell;
              TaskInfo.Parameter = NULL;
              TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
              TaskInfo.Priority  = TASK_PRIORITY_MEDIUM;
              TaskInfo.Flags     = 0;
              CreateTask(&KernelProcess, &TaskInfo);
            */
        } break;
    }
}

/***************************************************************************/

static void SendKeyCodeToBuffer(LPKEYCODE KeyCode) {
    U32 Index;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[SendKeyCodeToBuffer] Enter"));
#endif

    if (KeyCode->VirtualKey != 0 || KeyCode->ASCIICode != 0) {
        //-------------------------------------
        // Put the key in the buffer

        for (Index = 0; Index < MAXKEYBUFFER; Index++) {
            if (Keyboard.Buffer[Index].VirtualKey == 0 && Keyboard.Buffer[Index].ASCIICode == 0) {
                Keyboard.Buffer[Index] = *KeyCode;
                break;
            }
        }
    }

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[SendKeyCodeToBuffer] Exit"));
#endif
}

/***************************************************************************/

static void UpdateKeyboardLEDs(void) {
    U32 LED = 0;

    if (Keyboard.CapsLock) LED |= KSL_CAPS;
    if (Keyboard.NumLock) LED |= KSL_NUM;
    if (Keyboard.ScrollLock) LED |= KSL_SCROLL;

    SendKeyboardCommand(KSC_SETLEDSTATUS, LED);
}

/***************************************************************************/

static U32 GetKeyboardLEDs(void) {
    U32 LED = 0;

    if (Keyboard.CapsLock) LED |= KSL_CAPS;
    if (Keyboard.NumLock) LED |= KSL_NUM;
    if (Keyboard.ScrollLock) LED |= KSL_SCROLL;

    return LED;
}

/***************************************************************************/

static U32 SetKeyboardLEDs(U32 LED) {
    Keyboard.CapsLock = 0;
    Keyboard.NumLock = 0;
    Keyboard.ScrollLock = 0;

    if (LED & KSL_CAPS) Keyboard.CapsLock = 1;
    if (LED & KSL_NUM) Keyboard.NumLock = 1;
    if (LED & KSL_SCROLL) Keyboard.ScrollLock = 1;

    UpdateKeyboardLEDs();

    return TRUE;
}

/***************************************************************************/

static void HandleScanCode(U32 ScanCode) {
    static U32 PreviousCode = 0;
    static KEYCODE KeyCode;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[HandleScanCode] Enter"));
#endif

    if (ScanCode == 0) {
        PreviousCode = 0;
        return;
    }

    if (ScanCode == 0xE0 || ScanCode == 0xE1) {
        PreviousCode = ScanCode;
        return;
    }

    //-------------------------------------
    // Process special keys or translate
    // the scan code to an ASCII code

    if (PreviousCode == 0xE0) {
        PreviousCode = 0;

        if (ScanCode & 0x80) {
        } else {
            ScanCodeToKeyCode_E0(ScanCode, &KeyCode);
            SendKeyCodeToBuffer(&KeyCode);
        }
    } else if (PreviousCode == 0xE1) {
        PreviousCode = 0;

        if (ScanCode == 0x1D) PreviousCode = ScanCode;
    } else if (PreviousCode == 0x1D) {
        PreviousCode = 0;

        if (ScanCode & 0x80) {
        } else {
            ScanCodeToKeyCode_E1(ScanCode, &KeyCode);
            SendKeyCodeToBuffer(&KeyCode);
        }
    } else {
        PreviousCode = 0;

        if (ScanCode & 0x80) {
            //-------------------------------------
            // Turn off the key for async calls

            Keyboard.Status[ScanCode & 0x7F] = 0;
        } else {
            //-------------------------------------
            // Turn on the key for async calls

            Keyboard.Status[ScanCode] = 1;

            switch (ScanCode) {
                case SCAN_NUM_LOCK: {
                    Keyboard.NumLock = 1 - Keyboard.NumLock;
                    UpdateKeyboardLEDs();
                } break;

                case SCAN_CAPS_LOCK: {
                    Keyboard.CapsLock = 1 - Keyboard.CapsLock;
                    UpdateKeyboardLEDs();
                } break;

                case SCAN_SCROLL_LOCK: {
                    Keyboard.ScrollLock = 1 - Keyboard.ScrollLock;
                    UpdateKeyboardLEDs();
                } break;

                default: {
                    ScanCodeToKeyCode(ScanCode, &KeyCode);
                    SendKeyCodeToBuffer(&KeyCode);

                    if (KeyCode.VirtualKey == VK_F9) {
                        if (Keyboard.Status[SCAN_CONTROL]) {
                            VESADriver.Command(DF_UNLOAD, 0);
                        } else {
                            TASKINFO TaskInfo;
                            TaskInfo.Header.Size = sizeof(TASKINFO);
                            TaskInfo.Header.Version = EXOS_ABI_VERSION;
                            TaskInfo.Header.Flags = 0;
                            TaskInfo.Func = Shell;
                            TaskInfo.Parameter = NULL;
                            TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
                            TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
                            TaskInfo.Flags = 0;
                            CreateTask(&KernelProcess, &TaskInfo);
                        }
                    }
                } break;
            }
        }
    }

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[HandleScanCode] Exit"));
#endif
}

/***************************************************************************/

BOOL PeekChar(void) {
    U32 Result = FALSE;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[PeekChar] Enter"));
#endif

    LockMutex(&(Keyboard.Mutex), INFINITY);

    if (Keyboard.Buffer[0].VirtualKey) Result = TRUE;
    if (Keyboard.Buffer[0].ASCIICode) Result = TRUE;

    UnlockMutex(&(Keyboard.Mutex));

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[PeekChar] Exit"));
#endif

    return Result;
}

/***************************************************************************/

STR GetChar(void) {
    U32 Index;
    STR Char;

    LockMutex(&(Keyboard.Mutex), INFINITY);

    Char = Keyboard.Buffer[0].ASCIICode;

    //-------------------------------------
    // Roll the keyboard buffer

    for (Index = 1; Index < MAXKEYBUFFER; Index++) {
        Keyboard.Buffer[Index - 1] = Keyboard.Buffer[Index];
    }

    Keyboard.Buffer[MAXKEYBUFFER - 1].VirtualKey = 0;
    Keyboard.Buffer[MAXKEYBUFFER - 1].ASCIICode = 0;

    UnlockMutex(&(Keyboard.Mutex));

    return Char;
}

/***************************************************************************/

BOOL GetKeyCode(LPKEYCODE KeyCode) {
    U32 Index;

    LockMutex(&(Keyboard.Mutex), INFINITY);

    KeyCode->VirtualKey = Keyboard.Buffer[0].VirtualKey;
    KeyCode->ASCIICode = Keyboard.Buffer[0].ASCIICode;

    //-------------------------------------
    // Roll the keyboard buffer

    for (Index = 1; Index < MAXKEYBUFFER; Index++) {
        Keyboard.Buffer[Index - 1] = Keyboard.Buffer[Index];
    }

    Keyboard.Buffer[MAXKEYBUFFER - 1].VirtualKey = 0;
    Keyboard.Buffer[MAXKEYBUFFER - 1].ASCIICode = 0;

    UnlockMutex(&(Keyboard.Mutex));

    return TRUE;
}

/***************************************************************************/

BOOL GetKeyCodeDown(KEYCODE KeyCode) {
    U32 Index;

    switch (KeyCode.VirtualKey) {
        case VK_LSHIFT:
            return Keyboard.Status[SCAN_LEFT_SHIFT] != 0;
        case VK_RSHIFT:
            return Keyboard.Status[SCAN_RIGHT_SHIFT] != 0;
        case VK_LCTRL:
            return Keyboard.Status[SCAN_CONTROL] != 0;
        case VK_RCTRL:
            return Keyboard.Status[SCAN_RIGHT_CONTROL] != 0;
        case VK_CONTROL:
            return Keyboard.Status[SCAN_CONTROL] != 0 || Keyboard.Status[SCAN_RIGHT_CONTROL] != 0;
        case VK_SHIFT:
            return Keyboard.Status[SCAN_LEFT_SHIFT] != 0 || Keyboard.Status[SCAN_RIGHT_SHIFT] != 0;
        case VK_ALT:
            return Keyboard.Status[SCAN_ALT] != 0 || Keyboard.Status[SCAN_RIGHT_ALT] != 0;
        case VK_LALT:
            return Keyboard.Status[SCAN_ALT] != 0;
        case VK_RALT:
            return Keyboard.Status[SCAN_RIGHT_ALT] != 0;
        default:
            if (ScanCodeMap) {
                for (Index = 0; Index < KEYTABSIZE; Index++) {
                    if (ScanCodeMap[Index].Normal.VirtualKey == KeyCode.VirtualKey ||
                        ScanCodeMap[Index].Shift.VirtualKey == KeyCode.VirtualKey ||
                        ScanCodeMap[Index].Alt.VirtualKey == KeyCode.VirtualKey) {
                        return Keyboard.Status[Index] != 0;
                    }
                }
            }
            break;
    }

    return FALSE;
}

/***************************************************************************/

void WaitKey(void) {
    ConsolePrint(TEXT("Press a key\n"));
    while (!PeekChar()) {
    }
    GetChar();
}

/***************************************************************************/

void KeyboardHandler(void) {
    static U32 Busy = 0;
    U32 Status, Code;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[KeyboardHandler] Enter"));
#endif

    if (Busy) {
#if SCHEDULING_DEBUG_OUTPUT == 1
        KernelLogText(LOG_DEBUG, TEXT("[KeyboardHandler] Busy, exiting"));
#endif

        return;
    }

    Busy = 1;

    Status = InPortByte(KEYBOARD_COMMAND);

    do {
        if (Status & KSR_OUT_ERROR) break;

        Code = InPortByte(KEYBOARD_DATA);

        if (Status & KSR_OUT_FULL) {
            HandleScanCode(Code);
        }

        Status = InPortByte(KEYBOARD_COMMAND);
    } while (Status & KSR_OUT_FULL);

    Busy = 0;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[KeyboardHandler] Exit"));
#endif
}

/***************************************************************************/

static U32 InitializeKeyboard(void) {
    //-------------------------------------
    // Initialize the keyboard structure

    InitMutex(&(Keyboard.Mutex));

    Keyboard.NumLock = 1;

    //-------------------------------------
    // Enable the keyboard

    SendKeyboardCommand(KSC_ENABLE, KSC_ENABLE);

    //-------------------------------------
    // Set the LED status

    UpdateKeyboardLEDs();

    InPortByte(KEYBOARD_COMMAND);
    InPortByte(KEYBOARD_COMMAND);
    InPortByte(KEYBOARD_COMMAND);
    InPortByte(KEYBOARD_COMMAND);

    InPortByte(KEYBOARD_DATA);
    InPortByte(KEYBOARD_DATA);
    InPortByte(KEYBOARD_DATA);
    InPortByte(KEYBOARD_DATA);

    //-------------------------------------
    // Enable the keyboard's IRQ

    EnableIRQ(KEYBOARD_IRQ);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

U32 StdKeyboardCommands(U32 Function, U32 Parameter) {
    switch (Function) {
        case DF_LOAD:
            return InitializeKeyboard();
        case DF_GETVERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_GETLASTFUNC:
            return 0;
        case DF_KEY_GETSTATE:
            return 0;
        case DF_KEY_ISKEY:
            return (U32)PeekChar();
        case DF_KEY_GETKEY:
            return (U32)GetKeyCode((LPKEYCODE)Parameter);
        case DF_KEY_GETLED:
            return (U32)GetKeyboardLEDs();
        case DF_KEY_SETLED:
            return (U32)SetKeyboardLEDs(Parameter);
        case DF_KEY_GETDELAY:
            return 0;
        case DF_KEY_SETDELAY:
            return 0;
        case DF_KEY_GETRATE:
            return 0;
        case DF_KEY_SETRATE:
            return 0;
    }

    return MAX_U32;
}

/***************************************************************************/
