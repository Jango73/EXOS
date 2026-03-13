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

#include "console/Console.h"
#include "console/Console-VGATextFallback.h"
#include "DriverGetters.h"
#include "GFX.h"
#include "KernelData.h"
#include "Log.h"
#include "Mutex.h"
#include "Desktop.h"

/************************************************************************/

static BOOL DisplaySessionQueryGraphicsMode(LPDRIVER Driver, LPGRAPHICSMODEINFO ModeInfo);
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
    ModeInfo->ModeIndex = INFINITY;
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
        Session->ActiveMode = *ModeInfo;
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
        Session->HasValidMode = TRUE;
        SetActiveDesktop(NULL);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Update console front-end state to render text through a graphics backend.
 * @param GraphicsDriver Active graphics backend.
 * @param ModeInfo Active graphics mode.
 * @return TRUE on success.
 */
BOOL DisplaySessionSetConsoleGraphicsMode(LPDRIVER GraphicsDriver, LPGRAPHICSMODEINFO ModeInfo) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    if (GraphicsDriver == NULL || ModeInfo == NULL) {
        return FALSE;
    }

    if (ConsoleSetGraphicsTextMode(ModeInfo) == FALSE) {
        return FALSE;
    }

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            DisplaySessionInitialize();
        }

        Session->GraphicsDriver = GraphicsDriver;
        Session->ActiveMode = *ModeInfo;
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
        Session->HasValidMode = TRUE;
        SetActiveDesktop(NULL);
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
    ModeInfo.ModeIndex = INFINITY;
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
        if (ConsoleSetGraphicsTextMode(&ModeInfo) == FALSE) {
            goto FallbackToVgaText;
        }

        Session = GetDisplaySession();

        SAFE_USE(Session) {
            if (Session->IsInitialized == FALSE) {
                DisplaySessionInitialize();
            }

            Session->GraphicsDriver = GraphicsDriver;
            Session->ActiveMode = ModeInfo;
            Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
            Session->HasValidMode = TRUE;
            return TRUE;
        }
    }

FallbackToVgaText:
    WARNING(TEXT("[DisplaySwitchToConsole] Entering emergency VGA text fallback"));
    if (ConsoleVGATextFallbackActivate(80, 25, &ModeInfo) != FALSE) {
        WARNING(TEXT("[DisplaySwitchToConsole] Emergency VGA text fallback active (%ux%u)"),
                ModeInfo.Width,
                ModeInfo.Height);
        return TRUE;
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
