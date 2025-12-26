
/************************************************************************\

    EXOS Sample program
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


    Mouse Info - Display mouse position and buttons in real time

\************************************************************************/

#include "../../../runtime/include/exos-runtime.h"
#include "../../../runtime/include/exos.h"

/************************************************************************/

/**
 * @brief Render current mouse state at the top of the console.
 *
 * @param PosX Mouse X position.
 * @param PosY Mouse Y position.
 * @param Buttons Button state bitmask (MB_*).
 */
static void UpdateMouseDisplay(I32 PosX, I32 PosY, U32 Buttons) {
    POINT Cursor;
    U32 Left = (Buttons & MB_LEFT) ? 1 : 0;
    U32 Right = (Buttons & MB_RIGHT) ? 1 : 0;
    U32 Middle = (Buttons & MB_MIDDLE) ? 1 : 0;

    Cursor.X = 0;
    Cursor.Y = 0;
    ConsoleGotoXY(&Cursor);

    printf("Mouse position: X=%d Y=%d            \n", PosX, PosY);
    printf("Buttons: L=%u R=%u M=%u               \n", Left, Right, Middle);
}

/************************************************************************/

/**
 * @brief Entry point for the mouse info application.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return Exit code.
 */
int exosmain(int argc, char** argv) {
    MESSAGE Message;
    I32 PosX = 0;
    I32 PosY = 0;
    U32 Buttons = 0;

    UNUSED(argc);
    UNUSED(argv);

    ConsoleClear();
    UpdateMouseDisplay(PosX, PosY, Buttons);

    while (GetMessage(NULL, &Message, 0, 0)) {
        switch (Message.Message) {
            case EWM_MOUSEMOVE:
                PosX = (I32)Message.Param1;
                PosY = (I32)Message.Param2;
                UpdateMouseDisplay(PosX, PosY, Buttons);
                break;

            case EWM_MOUSEDOWN:
                Buttons |= Message.Param1;
                UpdateMouseDisplay(PosX, PosY, Buttons);
                break;

            case EWM_MOUSEUP:
                Buttons &= ~Message.Param1;
                UpdateMouseDisplay(PosX, PosY, Buttons);
                break;

            default:
                break;
        }
    }

    return 0;
}

/************************************************************************/
