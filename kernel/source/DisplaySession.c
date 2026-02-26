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


    Display session state

\************************************************************************/

#include "DisplaySession.h"

#include "Console.h"
#include "DriverGetters.h"
#include "GFX.h"
#include "KernelData.h"
#include "Log.h"
#include "Mutex.h"

/************************************************************************/

static BOOL DisplaySessionQueryGraphicsMode(LPDRIVER Driver, LPGRAPHICSMODEINFO ModeInfo);
static void DisplaySessionSetMainDesktopState(LPDRIVER GraphicsDriver, LPGRAPHICSMODEINFO ModeInfo);

/************************************************************************/

/**
 * @brief Query active mode information from a graphics backend.
 * @param Driver Graphics driver to query.
 * @param ModeInfo Output mode information.
 * @return TRUE when mode information is valid.
 */
static BOOL DisplaySessionQueryGraphicsMode(LPDRIVER Driver, LPGRAPHICSMODEINFO ModeInfo) {
    UINT Result;

    if (Driver == NULL || Driver->Command == NULL || ModeInfo == NULL) {
        return FALSE;
    }

    ModeInfo->Header.Size = sizeof(GRAPHICSMODEINFO);
    ModeInfo->Header.Version = EXOS_ABI_VERSION;
    ModeInfo->Header.Flags = 0;
    ModeInfo->Width = 0;
    ModeInfo->Height = 0;
    ModeInfo->BitsPerPixel = 0;

    Result = Driver->Command(DF_GFX_GETMODEINFO, (UINT)(LPVOID)ModeInfo);
    if (Result != DF_RETURN_SUCCESS || ModeInfo->Width == 0 || ModeInfo->Height == 0) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Keep main desktop metadata coherent with active front-end.
 * @param GraphicsDriver Driver used by the active front-end.
 * @param ModeInfo Active mode info used for root window bounds.
 */
static void DisplaySessionSetMainDesktopState(LPDRIVER GraphicsDriver, LPGRAPHICSMODEINFO ModeInfo) {
    RECT Rect;

    if (GraphicsDriver == NULL || ModeInfo == NULL || ModeInfo->Width == 0 || ModeInfo->Height == 0) {
        return;
    }

    Rect.X1 = 0;
    Rect.Y1 = 0;
    Rect.X2 = (I32)ModeInfo->Width - 1;
    Rect.Y2 = (I32)ModeInfo->Height - 1;

    SAFE_USE_VALID_ID(&MainDesktop, KOID_DESKTOP) {
        LockMutex(&(MainDesktop.Mutex), INFINITY);
        MainDesktop.Graphics = GraphicsDriver;
        MainDesktop.Mode = DESKTOP_MODE_CONSOLE;

        SAFE_USE_VALID_ID(MainDesktop.Window, KOID_WINDOW) {
            LockMutex(&(MainDesktop.Window->Mutex), INFINITY);
            MainDesktop.Window->Rect = Rect;
            MainDesktop.Window->ScreenRect = Rect;
            MainDesktop.Window->InvalidRect = Rect;
            UnlockMutex(&(MainDesktop.Window->Mutex));
        }

        UnlockMutex(&(MainDesktop.Mutex));
    }
}

/************************************************************************/

/**
 * @brief Initialize display session state once.
 */
void DisplaySessionInitialize(void) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    SAFE_USE(Session) {
        if (Session->IsInitialized != FALSE) {
            return;
        }

        MemorySet(Session, 0, sizeof(DISPLAY_SESSION));
        Session->GraphicsDriver = ConsoleGetDriver();
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
        Session->IsInitialized = TRUE;
    }
}

/************************************************************************/

/**
 * @brief Update display session state for console ownership.
 * @param ModeInfo Active console mode.
 * @return TRUE on success.
 */
BOOL DisplaySessionSetConsoleMode(LPGRAPHICSMODEINFO ModeInfo) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    if (ModeInfo == NULL) {
        return FALSE;
    }

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            DisplaySessionInitialize();
        }

        Session->GraphicsDriver = ConsoleGetDriver();
        Session->ActiveDesktop = &MainDesktop;
        Session->ActiveMode = *ModeInfo;
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
        Session->HasValidMode = TRUE;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Update display session state for desktop ownership.
 * @param Desktop Active desktop.
 * @param GraphicsDriver Selected graphics backend.
 * @param ModeInfo Active graphics mode.
 * @return TRUE on success.
 */
BOOL DisplaySessionSetDesktopMode(LPDESKTOP Desktop, LPDRIVER GraphicsDriver, LPGRAPHICSMODEINFO ModeInfo) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    if (Desktop == NULL || GraphicsDriver == NULL || ModeInfo == NULL) {
        return FALSE;
    }

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            DisplaySessionInitialize();
        }

        Session->GraphicsDriver = GraphicsDriver;
        Session->ActiveDesktop = Desktop;
        Session->ActiveMode = *ModeInfo;
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_DESKTOP;
        Session->HasValidMode = TRUE;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Switch display ownership to console front-end.
 * @return TRUE on success.
 */
BOOL DisplaySwitchToConsole(void) {
    GRAPHICSMODEINFO ModeInfo;
    UINT Result;
    LPDRIVER GraphicsDriver;
    LPDISPLAY_SESSION Session;

    ModeInfo.Header.Size = sizeof(ModeInfo);
    ModeInfo.Header.Version = EXOS_ABI_VERSION;
    ModeInfo.Header.Flags = 0;
    ModeInfo.Width = (Console.Width != 0) ? Console.Width : 80;
    ModeInfo.Height = (Console.Height != 0) ? Console.Height : 25;
    ModeInfo.BitsPerPixel = 0;

    Result = ConsoleSetMode(&ModeInfo);
    if (Result == DF_RETURN_SUCCESS) {
        return TRUE;
    }

    GraphicsDriver = GetGraphicsDriver();
    if (GraphicsDriver != NULL && GraphicsDriver != ConsoleGetDriver() &&
        DisplaySessionQueryGraphicsMode(GraphicsDriver, &ModeInfo) != FALSE) {
        Session = GetDisplaySession();

        SAFE_USE(Session) {
            if (Session->IsInitialized == FALSE) {
                DisplaySessionInitialize();
            }

            Session->GraphicsDriver = GraphicsDriver;
            Session->ActiveDesktop = &MainDesktop;
            Session->ActiveMode = ModeInfo;
            Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
            Session->HasValidMode = TRUE;
            DisplaySessionSetMainDesktopState(GraphicsDriver, &ModeInfo);
            return TRUE;
        }
    }

    WARNING(TEXT("[DisplaySwitchToConsole] Unable to activate console mode (%u)"), Result);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Switch display ownership to desktop front-end.
 * @param Desktop Desktop to activate.
 * @return TRUE on success.
 */
BOOL DisplaySwitchToDesktop(LPDESKTOP Desktop) {
    if (Desktop == NULL) {
        return FALSE;
    }

    return ShowDesktop(Desktop);
}

/************************************************************************/

/**
 * @brief Retrieve active display mode from session.
 * @param ModeInfoOut Destination mode descriptor.
 * @return TRUE when a valid mode is available.
 */
BOOL DisplaySessionGetActiveMode(LPGRAPHICSMODEINFO ModeInfoOut) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    if (ModeInfoOut == NULL) {
        return FALSE;
    }

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE || Session->HasValidMode == FALSE) {
            return FALSE;
        }

        *ModeInfoOut = Session->ActiveMode;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Retrieve active display front-end.
 * @return One of DISPLAY_FRONTEND_*.
 */
U32 DisplaySessionGetActiveFrontEnd(void) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            return DISPLAY_FRONTEND_NONE;
        }

        return Session->ActiveFrontEnd;
    }

    return DISPLAY_FRONTEND_NONE;
}

/************************************************************************/

/**
 * @brief Retrieve active graphics driver tracked by session.
 * @return Active driver pointer or NULL.
 */
LPDRIVER DisplaySessionGetActiveGraphicsDriver(void) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            return NULL;
        }

        return Session->GraphicsDriver;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Retrieve active desktop tracked by session.
 * @return Active desktop pointer or NULL.
 */
LPDESKTOP DisplaySessionGetActiveDesktop(void) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            return NULL;
        }

        return Session->ActiveDesktop;
    }

    return NULL;
}

/************************************************************************/
