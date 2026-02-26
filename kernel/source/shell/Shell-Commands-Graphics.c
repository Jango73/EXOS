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


    Shell commands - graphics

\************************************************************************/

#include "shell/Shell-Commands-Private.h"
#include "DisplaySession.h"
#include "DriverGetters.h"
#include "GFX.h"

/***************************************************************************/

/**
 * @brief Restore text console after graphics smoke rendering.
 */
static void RestoreConsoleAfterGraphicsSmoke(void) {
    if (DisplaySwitchToConsole() != FALSE) {
        return;
    }

    ERROR(TEXT("[RestoreConsoleAfterGraphicsSmoke] Console restore failed"));
}

/***************************************************************************/

/**
 * @brief Window procedure for gfx smoke_test rendering.
 * @param Window Target window handle.
 * @param Message Window message identifier.
 * @param Param1 First message parameter.
 * @param Param2 Second message parameter.
 * @return Message-specific result.
 */
static U32 GfxSmokeWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    static const I32 GfxSmokeWindowWidth = 560;
    static const I32 GfxSmokeWindowHeight = 320;

    switch (Message) {
        case EWM_DRAW: {
            HANDLE GraphicsContext = NULL;
            RECTINFO RectangleInfo;
            LINEINFO LineInfo;

            GraphicsContext = GetWindowGC(Window);
            if (GraphicsContext == NULL) {
                return 0;
            }

            RectangleInfo.Header.Size = sizeof(RectangleInfo);
            RectangleInfo.Header.Version = EXOS_ABI_VERSION;
            RectangleInfo.Header.Flags = 0;
            RectangleInfo.GC = GraphicsContext;

            LineInfo.Header.Size = sizeof(LineInfo);
            LineInfo.Header.Version = EXOS_ABI_VERSION;
            LineInfo.Header.Flags = 0;
            LineInfo.GC = GraphicsContext;

            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_HIGHLIGHT));
            (void)SelectBrush(GraphicsContext, GetSystemBrush(SM_COLOR_TITLE_BAR));
            RectangleInfo.X1 = 0;
            RectangleInfo.Y1 = 0;
            RectangleInfo.X2 = GfxSmokeWindowWidth - 1;
            RectangleInfo.Y2 = 32;
            (void)Rectangle(&RectangleInfo);

            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_DARK_SHADOW));
            (void)SelectBrush(GraphicsContext, GetSystemBrush(SM_COLOR_CLIENT));
            RectangleInfo.X1 = 0;
            RectangleInfo.Y1 = 33;
            RectangleInfo.X2 = GfxSmokeWindowWidth - 1;
            RectangleInfo.Y2 = GfxSmokeWindowHeight - 1;
            (void)Rectangle(&RectangleInfo);

            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_SELECTION));
            LineInfo.X1 = 12;
            LineInfo.Y1 = 48;
            LineInfo.X2 = GfxSmokeWindowWidth - 20;
            LineInfo.Y2 = GfxSmokeWindowHeight - 19;
            (void)Line(&LineInfo);

            LineInfo.X1 = GfxSmokeWindowWidth - 20;
            LineInfo.Y1 = 48;
            LineInfo.X2 = 12;
            LineInfo.Y2 = GfxSmokeWindowHeight - 19;
            (void)Line(&LineInfo);

            (void)EndWindowDraw(Window);
            return 0;
        }

        default:
            return DefWindowFunc(Window, Message, Param1, Param2);
    }
}

/************************************************************************/

/**
 * @brief Parse one unsigned decimal component from a mode token.
 * @param Text Mode token text.
 * @param InOutIndex Parse cursor.
 * @param ValueOut Parsed value.
 * @return TRUE on success.
 */
static BOOL ParseGraphicsModeComponent(LPCSTR Text, UINT* InOutIndex, U32* ValueOut) {
    UINT Index = 0;
    U32 Value = 0;
    BOOL HasDigit = FALSE;

    if (Text == NULL || InOutIndex == NULL || ValueOut == NULL) {
        return FALSE;
    }

    Index = *InOutIndex;
    while (Text[Index] >= '0' && Text[Index] <= '9') {
        HasDigit = TRUE;
        Value = (Value * 10) + (U32)(Text[Index] - '0');
        Index++;
    }

    if (!HasDigit) {
        return FALSE;
    }

    *InOutIndex = Index;
    *ValueOut = Value;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Parse one graphics mode token formatted as WidthxHeightxBitsPerPixel.
 * @param Token Mode token string.
 * @param InfoOut Parsed mode info.
 * @return TRUE on success.
 */
static BOOL ParseGraphicsModeToken(LPCSTR Token, LPGRAPHICSMODEINFO InfoOut) {
    UINT Index = 0;
    U32 Width = 0;
    U32 Height = 0;
    U32 BitsPerPixel = 0;

    if (Token == NULL || InfoOut == NULL || StringLength(Token) == 0) {
        return FALSE;
    }

    if (!ParseGraphicsModeComponent(Token, &Index, &Width)) {
        return FALSE;
    }

    if (Token[Index] != 'x' && Token[Index] != 'X') {
        return FALSE;
    }
    Index++;

    if (!ParseGraphicsModeComponent(Token, &Index, &Height)) {
        return FALSE;
    }

    if (Token[Index] != 'x' && Token[Index] != 'X') {
        return FALSE;
    }
    Index++;

    if (!ParseGraphicsModeComponent(Token, &Index, &BitsPerPixel)) {
        return FALSE;
    }

    if (Token[Index] != STR_NULL) {
        return FALSE;
    }

    if (Width == 0 || Height == 0 || BitsPerPixel == 0) {
        return FALSE;
    }

    InfoOut->Header.Size = sizeof(GRAPHICSMODEINFO);
    InfoOut->Header.Version = EXOS_ABI_VERSION;
    InfoOut->Header.Flags = 0;
    InfoOut->Width = Width;
    InfoOut->Height = Height;
    InfoOut->BitsPerPixel = BitsPerPixel;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Print supported shell aliases for graphics backend selection.
 */
static void PrintSupportedGraphicsBackendAliases(void) {
    UINT PrintedCount = 0;
    LPLIST DriverList = GetDriverList();

    if (DriverList == NULL) {
        ConsolePrint(TEXT("none"));
        return;
    }

    for (LPLISTNODE Node = DriverList->First; Node; Node = Node->Next) {
        LPDRIVER Driver = (LPDRIVER)Node;

        if (Driver->Type != DRIVER_TYPE_GRAPHICS || Driver == GraphicsSelectorGetDriver()) {
            continue;
        }

        if (StringLength(Driver->Alias) == 0) {
            continue;
        }

        if (PrintedCount != 0) {
            ConsolePrint(TEXT("|"));
        }

        ConsolePrint(TEXT("%s"), Driver->Alias);
        PrintedCount++;
    }

    if (PrintedCount == 0) {
        ConsolePrint(TEXT("none"));
    }
}

/************************************************************************/

/**
 * @brief Find one graphics backend driver by alias.
 * @param Alias Driver alias.
 * @return Driver pointer or NULL when not found.
 */
static LPDRIVER FindGraphicsBackendByAlias(LPCSTR Alias) {
    LPLIST DriverList = GetDriverList();

    if (Alias == NULL || StringLength(Alias) == 0 || DriverList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = DriverList->First; Node; Node = Node->Next) {
        LPDRIVER Driver = (LPDRIVER)Node;

        if (Driver == NULL || Driver == GraphicsSelectorGetDriver()) {
            continue;
        }

        if (Driver->Type != DRIVER_TYPE_GRAPHICS || Driver->Command == NULL) {
            continue;
        }

        if (StringLength(Driver->Alias) == 0) {
            continue;
        }

        if (StringCompareNC(Driver->Alias, Alias) == 0) {
            return Driver;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Draw a temporary desktop/window and return to text console.
 * @param DurationMilliseconds Display duration.
 * @return DF_RETURN_SUCCESS on completion.
 */
static U32 RunGraphicsSmokeTest(U32 DurationMilliseconds) {
    LPDESKTOP Desktop = NULL;
    LPWINDOW Window = NULL;
    WINDOWINFO WindowInfo;

    Desktop = CreateDesktop();
    if (Desktop == NULL) {
        ConsolePrint(TEXT("gfx smoke_test: desktop creation failed\n"));
        return DF_RETURN_SUCCESS;
    }

    if (DisplaySwitchToDesktop(Desktop) == FALSE) {
        ConsolePrint(TEXT("gfx smoke_test: desktop show failed\n"));
        DeleteDesktop(Desktop);
        return DF_RETURN_SUCCESS;
    }

    WindowInfo.Header.Size = sizeof(WindowInfo);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = (HANDLE)Desktop->Window;
    WindowInfo.Function = GfxSmokeWindowFunc;
    WindowInfo.Style = EWS_VISIBLE;
    WindowInfo.ID = 0;
    WindowInfo.WindowPosition.X = 120;
    WindowInfo.WindowPosition.Y = 80;
    WindowInfo.WindowSize.X = 560;
    WindowInfo.WindowSize.Y = 320;
    WindowInfo.ShowHide = TRUE;

    Window = CreateWindow(&WindowInfo);
    if (Window == NULL) {
        ConsolePrint(TEXT("gfx smoke_test: window creation failed\n"));
        RestoreConsoleAfterGraphicsSmoke();
        DeleteDesktop(Desktop);
        return DF_RETURN_SUCCESS;
    }

    (void)SendMessage((HANDLE)Window, EWM_DRAW, 0, 0);

    Sleep(DurationMilliseconds);

    RestoreConsoleAfterGraphicsSmoke();
    DeleteDesktop(Desktop);
    ConsolePrint(TEXT("gfx smoke_test: done\n"));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Graphics command dispatcher.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_gfx(LPSHELLCONTEXT Context) {
    STR Mode[64];
    STR DriverName[64];
    GRAPHICSMODEINFO ModeInfo;
    LPDRIVER GraphicsDriver = NULL;
    UINT ModeSetResult = 0;
    LPDESKTOP ActiveDesktop = NULL;
    LPCSTR ActiveBackendName = NULL;
    U32 DurationMilliseconds = 5000;
    LPDRIVER RequestedBackend = NULL;
    UINT RequestedBackendLoadResult = DF_RETURN_SUCCESS;

    ParseNextCommandLineComponent(Context);
    StringCopy(Mode, Context->Command);

    if (StringLength(Mode) == 0) {
        ConsolePrint(TEXT("Usage: gfx backend Driver WidthxHeightxBitsPerPixel\n"));
        ConsolePrint(TEXT("       gfx smoke_test [DurationMilliseconds]\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Mode, TEXT("smoke_test")) == 0) {
        ParseNextCommandLineComponent(Context);
        if (StringLength(Context->Command) != 0) {
            DurationMilliseconds = StringToU32(Context->Command);
            if (DurationMilliseconds == 0) {
                ConsolePrint(TEXT("Usage: gfx smoke_test [DurationMilliseconds]\n"));
                return DF_RETURN_SUCCESS;
            }
        }

        return RunGraphicsSmokeTest(DurationMilliseconds);
    }

    if (StringCompareNC(Mode, TEXT("backend")) != 0) {
        ConsolePrint(TEXT("Usage: gfx backend Driver WidthxHeightxBitsPerPixel\n"));
        ConsolePrint(TEXT("       gfx smoke_test [DurationMilliseconds]\n"));
        return DF_RETURN_SUCCESS;
    }

    ParseNextCommandLineComponent(Context);
    StringCopy(DriverName, Context->Command);
    ParseNextCommandLineComponent(Context);

    if (StringLength(DriverName) == 0 || StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: gfx backend Driver WidthxHeightxBitsPerPixel\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!ParseGraphicsModeToken(Context->Command, &ModeInfo)) {
        ConsolePrint(TEXT("Usage: gfx backend Driver WidthxHeightxBitsPerPixel\n"));
        return DF_RETURN_SUCCESS;
    }

    RequestedBackend = FindGraphicsBackendByAlias(DriverName);
    if (RequestedBackend != NULL && (RequestedBackend->Flags & DRIVER_FLAG_READY) == 0) {
        RequestedBackendLoadResult = RequestedBackend->Command(DF_LOAD, 0);
    }

    if (GraphicsSelectorGetDriver() != NULL && GraphicsSelectorGetDriver()->Command != NULL) {
        (void)GraphicsSelectorGetDriver()->Command(DF_UNLOAD, 0);
    }

    if (!GraphicsSelectorForceBackendByName(DriverName)) {
        ConsolePrint(TEXT("gfx: backend '%s' unavailable (supported: "), DriverName);
        PrintSupportedGraphicsBackendAliases();
        ConsolePrint(TEXT(")\n"));
        if (RequestedBackend != NULL) {
            ConsolePrint(TEXT("gfx: backend '%s' load_result=%u ready=%u\n"),
                DriverName,
                RequestedBackendLoadResult,
                (RequestedBackend->Flags & DRIVER_FLAG_READY) != 0 ? 1 : 0);
            if (StringCompareNC(DriverName, TEXT("igpu")) == 0) {
                ConsolePrint(TEXT("gfx: check logs [IntelGfxLoad] and [IntelGfxTakeoverActiveMode]\n"));
            }
        }
        return DF_RETURN_SUCCESS;
    }

    GraphicsDriver = GetGraphicsDriver();
    if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
        ConsolePrint(TEXT("gfx: no graphics driver available\n"));
        return DF_RETURN_SUCCESS;
    }

    ModeSetResult = GraphicsDriver->Command(DF_GFX_SETMODE, (UINT)(LPVOID)&ModeInfo);
    if (ModeSetResult != DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("gfx: mode set failed (%u)\n"), ModeSetResult);
        return DF_RETURN_SUCCESS;
    }

    ActiveDesktop = DisplaySessionGetActiveDesktop();
    if (ActiveDesktop != NULL) {
        (void)DisplaySessionSetDesktopMode(ActiveDesktop, GraphicsDriver, &ModeInfo);
    }

    ActiveBackendName = GraphicsSelectorGetActiveBackendName();
    if (ActiveBackendName != NULL && StringLength(ActiveBackendName) != 0) {
        ConsolePrint(TEXT("gfx: backend=%s mode=%ux%ux%u\n"),
            ActiveBackendName,
            ModeInfo.Width,
            ModeInfo.Height,
            ModeInfo.BitsPerPixel);
    } else {
        ConsolePrint(TEXT("gfx: mode=%ux%ux%u\n"),
            ModeInfo.Width,
            ModeInfo.Height,
            ModeInfo.BitsPerPixel);
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/
