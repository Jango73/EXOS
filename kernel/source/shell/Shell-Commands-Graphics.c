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
#include "Desktop.h"
#include "Desktop-InternalTest.h"

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
    InfoOut->ModeIndex = INFINITY;
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

    (void)PostMessage((HANDLE)Window, EWM_DRAW, 0, 0);

    Sleep(DurationMilliseconds);

    RestoreConsoleAfterGraphicsSmoke();
    DeleteDesktop(Desktop);
    ConsolePrint(TEXT("gfx smoke_test: done\n"));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Convert display front-end identifier to text.
 * @param FrontEnd Display front-end value.
 * @return Constant text description.
 */
static LPCSTR DesktopFrontEndToText(U32 FrontEnd) {
    switch (FrontEnd) {
        case DISPLAY_FRONTEND_CONSOLE:
            return TEXT("console");
        case DISPLAY_FRONTEND_DESKTOP:
            return TEXT("desktop");
        default:
            return TEXT("none");
    }
}

/************************************************************************/

/**
 * @brief Convert desktop mode identifier to text.
 * @param Mode Desktop mode value.
 * @return Constant text description.
 */
static LPCSTR DesktopModeToText(U32 Mode) {
    switch (Mode) {
        case DESKTOP_MODE_GRAPHICS:
            return TEXT("graphics");
        case DESKTOP_MODE_CONSOLE:
            return TEXT("console");
        default:
            return TEXT("unknown");
    }
}

/************************************************************************/

/**
 * @brief Convert theme fallback reason identifier to text.
 * @param Reason Theme fallback reason.
 * @return Constant text description.
 */
static LPCSTR ThemeFallbackReasonToText(U32 Reason) {
    switch (Reason) {
        case DESKTOP_THEME_FALLBACK_REASON_NONE:
            return TEXT("none");
        case DESKTOP_THEME_FALLBACK_REASON_FILE_READ_FAILED:
            return TEXT("file_read_failed");
        case DESKTOP_THEME_FALLBACK_REASON_PARSE_FAILED:
            return TEXT("parse_failed");
        case DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED:
            return TEXT("activation_failed");
        case DESKTOP_THEME_FALLBACK_REASON_NO_STAGED_THEME:
            return TEXT("no_staged_theme");
        case DESKTOP_THEME_FALLBACK_REASON_RESET_TO_DEFAULT:
            return TEXT("reset_to_default");
        default:
            return TEXT("unknown");
    }
}

/************************************************************************/

/**
 * @brief Convert desktop cursor path identifier to text.
 * @param Path Cursor path value.
 * @return Constant text description.
 */
static LPCSTR CursorPathToText(U32 Path) {
    switch (Path) {
        case DESKTOP_CURSOR_PATH_HARDWARE:
            return TEXT("hardware");
        case DESKTOP_CURSOR_PATH_SOFTWARE:
            return TEXT("software");
        default:
            return TEXT("unset");
    }
}

/************************************************************************/

/**
 * @brief Convert desktop cursor fallback reason to text.
 * @param Reason Cursor fallback reason.
 * @return Constant text description.
 */
static LPCSTR CursorFallbackReasonToText(U32 Reason) {
    switch (Reason) {
        case DESKTOP_CURSOR_FALLBACK_NONE:
            return TEXT("none");
        case DESKTOP_CURSOR_FALLBACK_NOT_GRAPHICS:
            return TEXT("not_graphics");
        case DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES:
            return TEXT("no_capabilities");
        case DESKTOP_CURSOR_FALLBACK_NO_CURSOR_PLANE:
            return TEXT("no_cursor_plane");
        case DESKTOP_CURSOR_FALLBACK_SET_SHAPE_FAILED:
            return TEXT("set_shape_failed");
        case DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED:
            return TEXT("set_position_failed");
        case DESKTOP_CURSOR_FALLBACK_SET_VISIBLE_FAILED:
            return TEXT("set_visible_failed");
        default:
            return TEXT("unknown");
    }
}

/************************************************************************/

/**
 * @brief Print desktop/theme runtime status.
 */
static void PrintDesktopStatus(void) {
    U32 FrontEnd;
    LPDESKTOP ActiveDesktop;
    DESKTOP_THEME_RUNTIME_INFO ThemeInfo;

    FrontEnd = DisplaySessionGetActiveFrontEnd();
    ActiveDesktop = DisplaySessionGetActiveDesktop();

    ConsolePrint(TEXT("desktop: front_end=%s\n"), DesktopFrontEndToText(FrontEnd));

    if (ActiveDesktop != NULL) {
        ConsolePrint(TEXT("desktop: mode=%s\n"), DesktopModeToText(ActiveDesktop->Mode));
        ConsolePrint(TEXT("desktop: cursor_path=%s visible=%u pos=(%x,%x)\n"),
            CursorPathToText(ActiveDesktop->Cursor.RenderPath),
            ActiveDesktop->Cursor.Visible ? 1 : 0,
            UNSIGNED(ActiveDesktop->Cursor.X),
            UNSIGNED(ActiveDesktop->Cursor.Y));
        ConsolePrint(TEXT("desktop: cursor_size=%ux%u\n"),
            ActiveDesktop->Cursor.Width,
            ActiveDesktop->Cursor.Height);
        ConsolePrint(TEXT("desktop: cursor_fallback=%s\n"),
            CursorFallbackReasonToText(ActiveDesktop->Cursor.FallbackReason));
    } else {
        ConsolePrint(TEXT("desktop: mode=unknown\n"));
    }

    if (GetActiveThemeInfo(&ThemeInfo) == FALSE) {
        ConsolePrint(TEXT("desktop: theme=unavailable\n"));
        return;
    }

    ConsolePrint(TEXT("desktop: theme_source=%s\n"), ThemeInfo.IsLoadedActive ? TEXT("loaded") : TEXT("built-in"));
    if (ThemeInfo.ActiveThemePath[0] != STR_NULL) {
        ConsolePrint(TEXT("desktop: active_theme=%s\n"), ThemeInfo.ActiveThemePath);
    }
    if (ThemeInfo.HasStagedTheme != FALSE && ThemeInfo.StagedThemePath[0] != STR_NULL) {
        ConsolePrint(TEXT("desktop: staged_theme=%s\n"), ThemeInfo.StagedThemePath);
    }
    ConsolePrint(TEXT("desktop: theme_last_status=%x fallback_reason=%s\n"),
        ThemeInfo.LastStatus,
        ThemeFallbackReasonToText(ThemeInfo.LastFallbackReason));
}

/************************************************************************/

/**
 * @brief Show main desktop from shell and optionally apply config-selected theme.
 * @return DF_RETURN_SUCCESS on completion.
 */
static U32 ShowMainDesktopFromShell(void) {
    LPDESKTOP Desktop;
    LPCSTR ConfiguredThemePath;

    Desktop = &MainDesktop;

    if (DisplaySwitchToDesktop(Desktop) == FALSE) {
        ConsolePrint(TEXT("desktop show: unable to switch to main desktop\n"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("desktop show: main desktop active\n"));

    // A configured theme path is optional and must never block desktop activation.
    ConfiguredThemePath = GetConfigurationValue(TEXT("Desktop.ThemePath"));
    if (ConfiguredThemePath != NULL && StringLength(ConfiguredThemePath) != 0) {
        if (LoadTheme(ConfiguredThemePath) && ActivateTheme(TEXT("staged"))) {
            ConsolePrint(TEXT("desktop show: theme activated from config (%s)\n"), ConfiguredThemePath);
        } else {
            ConsolePrint(TEXT("desktop show: theme config failed, keeping current theme (%s)\n"), ConfiguredThemePath);
        }
    }

    if (DesktopInternalTestEnsureWindowsVisible(Desktop) == FALSE) {
        ConsolePrint(TEXT("desktop show: internal test windows unavailable\n"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Apply one theme command target.
 * @param Target Path or name provided by shell.
 * @return DF_RETURN_SUCCESS on completion.
 */
static U32 ApplyDesktopThemeTarget(LPCSTR Target) {
    if (Target == NULL || StringLength(Target) == 0) {
        ConsolePrint(TEXT("Usage: desktop theme <path-or-name>\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Target, TEXT("default")) == 0 ||
        StringCompareNC(Target, TEXT("builtin")) == 0 ||
        StringCompareNC(Target, TEXT("built-in")) == 0) {
        if (ResetThemeToDefault()) {
            ConsolePrint(TEXT("desktop theme: built-in theme activated\n"));
        } else {
            ConsolePrint(TEXT("desktop theme: unable to activate built-in theme\n"));
        }
        return DF_RETURN_SUCCESS;
    }

    if (ActivateTheme(Target)) {
        ConsolePrint(TEXT("desktop theme: activated %s\n"), Target);
        return DF_RETURN_SUCCESS;
    }

    if (LoadTheme(Target) == FALSE) {
        ConsolePrint(TEXT("desktop theme: unable to load %s\n"), Target);
        return DF_RETURN_SUCCESS;
    }

    if (ActivateTheme(TEXT("staged")) == FALSE) {
        ConsolePrint(TEXT("desktop theme: load succeeded but activation failed (%s)\n"), Target);
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("desktop theme: loaded and activated %s\n"), Target);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Desktop command dispatcher.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_desktop(LPSHELLCONTEXT Context) {
    STR Action[64];

    ParseNextCommandLineComponent(Context);
    StringCopy(Action, Context->Command);

    if (StringLength(Action) == 0 || StringCompareNC(Action, TEXT("status")) == 0) {
        PrintDesktopStatus();
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Action, TEXT("show")) == 0) {
        return ShowMainDesktopFromShell();
    }

    if (StringCompareNC(Action, TEXT("theme")) == 0) {
        ParseNextCommandLineComponent(Context);
        return ApplyDesktopThemeTarget(Context->Command);
    }

    ConsolePrint(TEXT("Usage: desktop show\n"));
    ConsolePrint(TEXT("       desktop status\n"));
    ConsolePrint(TEXT("       desktop theme <path-or-name>\n"));
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
        StringCopy(Mode, TEXT("info"));
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

    if (StringCompareNC(Mode, TEXT("info")) == 0) {
        GraphicsDriver = GetGraphicsDriver();
        if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
            ConsolePrint(TEXT("gfx: no graphics driver available\n"));
            return DF_RETURN_SUCCESS;
        }

        ModeInfo.Header.Size = sizeof(ModeInfo);
        ModeInfo.Header.Version = EXOS_ABI_VERSION;
        ModeInfo.Header.Flags = 0;
        ModeInfo.ModeIndex = INFINITY;
        ModeInfo.Width = 0;
        ModeInfo.Height = 0;
        ModeInfo.BitsPerPixel = 0;

        ModeSetResult = GraphicsDriver->Command(DF_GFX_GETMODEINFO, (UINT)(LPVOID)&ModeInfo);
        if (ModeSetResult != DF_RETURN_SUCCESS) {
            ConsolePrint(TEXT("gfx: mode query failed (%u)\n"), ModeSetResult);
            return DF_RETURN_SUCCESS;
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

    if (StringCompareNC(Mode, TEXT("backend")) != 0) {
        ConsolePrint(TEXT("Usage: gfx backend Driver WidthxHeightxBitsPerPixel\n"));
        ConsolePrint(TEXT("       gfx info\n"));
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

    if (DisplaySessionSetConsoleGraphicsMode(GraphicsDriver, &ModeInfo) == FALSE) {
        ActiveDesktop = DisplaySessionGetActiveDesktop();
        if (ActiveDesktop != NULL) {
            (void)DisplaySessionSetDesktopMode(ActiveDesktop, GraphicsDriver, &ModeInfo);
        }
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
