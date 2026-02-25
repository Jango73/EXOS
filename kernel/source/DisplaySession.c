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

#include "DriverGetters.h"
#include "KernelData.h"
#include "Log.h"

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
