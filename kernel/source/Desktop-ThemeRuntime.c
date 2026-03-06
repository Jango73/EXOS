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


    Desktop theme runtime API

\************************************************************************/

#include "Desktop-ThemeRuntime.h"
#include "CoreString.h"
#include "Desktop.h"
#include "Desktop-ThemeTokens.h"
#include "File.h"
#include "Kernel.h"
#include "Log.h"

/***************************************************************************/

/**
 * @brief Resolve a desktop target for theme runtime operations.
 * @param Desktop Requested desktop or NULL for current desktop.
 * @return Valid desktop pointer or NULL.
 */
static LPDESKTOP ThemeResolveDesktop(LPDESKTOP Desktop) {
    LPPROCESS Process;

    if (Desktop != NULL && Desktop->TypeID == KOID_DESKTOP) return Desktop;

    Process = GetCurrentProcess();
    if (Process != NULL && Process->TypeID == KOID_PROCESS) {
        if (Process->Desktop != NULL && Process->Desktop->TypeID == KOID_DESKTOP) {
            return Process->Desktop;
        }
    }

    if (MainDesktop.TypeID == KOID_DESKTOP) return &MainDesktop;
    return NULL;
}

/***************************************************************************/

/**
 * @brief Ensure runtime pointers are initialized for one desktop.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL ThemeEnsureRuntimeState(LPDESKTOP Desktop) {
    LPDESKTOP_THEME_RUNTIME BuiltinRuntime;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    BuiltinRuntime = (LPDESKTOP_THEME_RUNTIME)Desktop->BuiltinThemeRuntime;

    if (BuiltinRuntime == NULL) {
        BuiltinRuntime = DesktopThemeCreateBuiltinRuntime();
        if (BuiltinRuntime == NULL) {
            Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_NO_MEMORY;
            Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED;
            return FALSE;
        }
        Desktop->BuiltinThemeRuntime = BuiltinRuntime;
    }

    if (Desktop->ActiveThemeRuntime == NULL) {
        Desktop->ActiveThemeRuntime = BuiltinRuntime;
        Desktop->ThemeActiveFromFile = FALSE;
        Desktop->ActiveThemePath[0] = STR_NULL;
    }

    if (Desktop->ThemeLastStatus == 0) {
        Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_SUCCESS;
        Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NONE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Find one runtime table entry value by key.
 * @param Entries Runtime table.
 * @param EntryCount Number of entries.
 * @param Key Entry key.
 * @param Value Receives value pointer.
 * @return TRUE when key exists.
 */
static BOOL ThemeFindRuntimeEntry(
    LPDESKTOP_THEME_TABLE_ENTRY Entries,
    U32 EntryCount,
    LPCSTR Key,
    LPCSTR* Value
) {
    U32 Index;

    if (Entries == NULL || Key == NULL || Value == NULL) return FALSE;

    for (Index = 0; Index < EntryCount; Index++) {
        if (Entries[Index].Key == NULL || Entries[Index].Value == NULL) continue;
        if (StringCompareNC(Entries[Index].Key, Key) != 0) continue;
        *Value = Entries[Index].Value;
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Invalidate one window and every child window for full redraw.
 * @param Window Root window of the invalidation traversal.
 */
static void ThemeInvalidateWindowTree(LPWINDOW Window) {
    LPLISTNODE Node;
    LPWINDOW Child;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    (void)InvalidateWindowRect((HANDLE)Window, NULL);

    LockMutex(&(Window->Mutex), INFINITY);

    for (Node = Window->Children != NULL ? Window->Children->First : NULL; Node != NULL; Node = Node->Next) {
        Child = (LPWINDOW)Node;
        if (Child != NULL && Child->TypeID == KOID_WINDOW) {
            ThemeInvalidateWindowTree(Child);
        }
    }

    UnlockMutex(&(Window->Mutex));
}

/***************************************************************************/

/**
 * @brief Invalidate all desktop windows after a theme switch.
 * @param Desktop Target desktop.
 */
static void ThemeInvalidateDesktopWindows(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;
    if (Desktop->Window == NULL || Desktop->Window->TypeID != KOID_WINDOW) return;

    ThemeInvalidateWindowTree(Desktop->Window);
}

/***************************************************************************/

BOOL LoadTheme(LPCSTR Path) {
    LPDESKTOP Desktop;
    LPSTR Source;
    UINT SourceSize = 0;
    LPDESKTOP_THEME_RUNTIME Candidate = NULL;
    LPDESKTOP_THEME_RUNTIME PreviousStaged;
    U32 Status = DESKTOP_THEME_STATUS_SUCCESS;

    Desktop = ThemeResolveDesktop(NULL);
    if (Desktop == NULL) return FALSE;
    if (ThemeEnsureRuntimeState(Desktop) == FALSE) return FALSE;

    if (Path == NULL || Path[0] == STR_NULL) {
        Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_BAD_PARAMETER;
        Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_FILE_READ_FAILED;
        WARNING(TEXT("[LoadTheme] Invalid theme path"));
        return FALSE;
    }

    Source = (LPSTR)FileReadAll(Path, &SourceSize);
    if (Source == NULL || SourceSize == 0) {
        Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_INVALID_TOML;
        Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_FILE_READ_FAILED;
        WARNING(TEXT("[LoadTheme] Cannot read theme file %s"), Path);
        if (Source != NULL) KernelHeapFree(Source);
        return FALSE;
    }

    if (DesktopThemeParseStrict(Source, &Candidate, &Status) == FALSE) {
        Desktop->ThemeLastStatus = Status;
        Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_PARSE_FAILED;
        WARNING(TEXT("[LoadTheme] Parse failed status=%x path=%s"), Status, Path);
        KernelHeapFree(Source);
        return FALSE;
    }

    PreviousStaged = (LPDESKTOP_THEME_RUNTIME)Desktop->StagedThemeRuntime;
    if (PreviousStaged != NULL &&
        PreviousStaged != (LPDESKTOP_THEME_RUNTIME)Desktop->ActiveThemeRuntime &&
        PreviousStaged != (LPDESKTOP_THEME_RUNTIME)Desktop->BuiltinThemeRuntime) {
        DesktopThemeFreeRuntime(PreviousStaged);
    }

    Desktop->StagedThemeRuntime = Candidate;
    StringCopy(Desktop->StagedThemePath, Path);
    Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_SUCCESS;
    Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NONE;

    DEBUG(TEXT("[LoadTheme] Theme loaded and staged path=%s"), Path);

    KernelHeapFree(Source);
    return TRUE;
}

/***************************************************************************/

BOOL ActivateTheme(LPCSTR NameOrHandle) {
    LPDESKTOP Desktop;
    LPDESKTOP_THEME_RUNTIME BuiltinRuntime;
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;
    LPDESKTOP_THEME_RUNTIME StagedRuntime;
    STR PathToActivate[MAX_FILE_NAME];

    Desktop = ThemeResolveDesktop(NULL);
    if (Desktop == NULL) return FALSE;
    if (ThemeEnsureRuntimeState(Desktop) == FALSE) return FALSE;

    if (NameOrHandle != NULL && NameOrHandle[0] != STR_NULL) {
        BOOL MatchesPath = (StringCompareNC(NameOrHandle, Desktop->StagedThemePath) == 0);
        BOOL MatchesAlias = (StringCompareNC(NameOrHandle, TEXT("staged")) == 0 || StringCompareNC(NameOrHandle, TEXT("loaded")) == 0);
        if (MatchesPath == FALSE && MatchesAlias == FALSE) {
            Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_BAD_PARAMETER;
            Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NO_STAGED_THEME;
            WARNING(TEXT("[ActivateTheme] Unknown theme handle %s"), NameOrHandle);
            return FALSE;
        }
    }

    StagedRuntime = (LPDESKTOP_THEME_RUNTIME)Desktop->StagedThemeRuntime;
    if (StagedRuntime == NULL) {
        Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_BAD_PARAMETER;
        Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NO_STAGED_THEME;
        WARNING(TEXT("[ActivateTheme] No staged theme available"));
        return FALSE;
    }

    StringCopy(PathToActivate, Desktop->StagedThemePath);

    BuiltinRuntime = (LPDESKTOP_THEME_RUNTIME)Desktop->BuiltinThemeRuntime;
    ActiveRuntime = (LPDESKTOP_THEME_RUNTIME)Desktop->ActiveThemeRuntime;

    if (DesktopThemeActivateParsed(StagedRuntime, BuiltinRuntime, &ActiveRuntime) == FALSE) {
        Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_NO_MEMORY;
        Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED;
        WARNING(TEXT("[ActivateTheme] Activation failed"));
        return FALSE;
    }

    Desktop->ActiveThemeRuntime = ActiveRuntime;
    Desktop->StagedThemeRuntime = NULL;
    Desktop->StagedThemePath[0] = STR_NULL;
    Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_SUCCESS;
    Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NONE;

    if (ActiveRuntime == BuiltinRuntime) {
        Desktop->ThemeActiveFromFile = FALSE;
        Desktop->ActiveThemePath[0] = STR_NULL;
    } else {
        Desktop->ThemeActiveFromFile = TRUE;
        StringCopy(Desktop->ActiveThemePath, PathToActivate);
    }

    DesktopThemeSyncSystemObjects();
    ThemeInvalidateDesktopWindows(Desktop);

    DEBUG(TEXT("[ActivateTheme] Theme activated path=%s"), Desktop->ThemeActiveFromFile ? Desktop->ActiveThemePath : TEXT("<built-in>"));
    return TRUE;
}

/***************************************************************************/

BOOL GetActiveThemeInfo(LPDESKTOP_THEME_RUNTIME_INFO Info) {
    LPDESKTOP Desktop;
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;
    LPDESKTOP_THEME_RUNTIME BuiltinRuntime;

    if (Info == NULL) return FALSE;

    Desktop = ThemeResolveDesktop(NULL);
    if (Desktop == NULL) return FALSE;
    if (ThemeEnsureRuntimeState(Desktop) == FALSE) return FALSE;

    MemorySet(Info, 0, sizeof(DESKTOP_THEME_RUNTIME_INFO));

    ActiveRuntime = (LPDESKTOP_THEME_RUNTIME)Desktop->ActiveThemeRuntime;
    BuiltinRuntime = (LPDESKTOP_THEME_RUNTIME)Desktop->BuiltinThemeRuntime;

    Info->IsBuiltinActive = (ActiveRuntime == BuiltinRuntime);
    Info->IsLoadedActive = Desktop->ThemeActiveFromFile;
    Info->HasStagedTheme = (Desktop->StagedThemeRuntime != NULL);
    StringCopy(Info->ActiveThemePath, Desktop->ActiveThemePath);
    StringCopy(Info->StagedThemePath, Desktop->StagedThemePath);
    Info->LastStatus = Desktop->ThemeLastStatus;
    Info->LastFallbackReason = Desktop->ThemeLastFallbackReason;

    if (ActiveRuntime != NULL) {
        Info->ActiveTokenCount = ActiveRuntime->TokenCount;
        Info->ActiveElementPropertyCount = ActiveRuntime->ElementPropertyCount;
        Info->ActiveRecipeCount = ActiveRuntime->RecipeCount;
        Info->ActiveBindingCount = ActiveRuntime->BindingCount;
    }

    return TRUE;
}

/***************************************************************************/

BOOL ResetThemeToDefault(void) {
    LPDESKTOP Desktop;
    LPDESKTOP_THEME_RUNTIME BuiltinRuntime;
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;

    Desktop = ThemeResolveDesktop(NULL);
    if (Desktop == NULL) return FALSE;
    if (ThemeEnsureRuntimeState(Desktop) == FALSE) return FALSE;

    BuiltinRuntime = (LPDESKTOP_THEME_RUNTIME)Desktop->BuiltinThemeRuntime;
    ActiveRuntime = (LPDESKTOP_THEME_RUNTIME)Desktop->ActiveThemeRuntime;

    if (DesktopThemeActivateParsed(NULL, BuiltinRuntime, &ActiveRuntime) == FALSE) {
        Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_NO_MEMORY;
        Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED;
        WARNING(TEXT("[ResetThemeToDefault] Reset failed"));
        return FALSE;
    }

    Desktop->ActiveThemeRuntime = ActiveRuntime;
    Desktop->ThemeActiveFromFile = FALSE;
    Desktop->ActiveThemePath[0] = STR_NULL;
    Desktop->ThemeLastStatus = DESKTOP_THEME_STATUS_SUCCESS;
    Desktop->ThemeLastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_RESET_TO_DEFAULT;

    DesktopThemeSyncSystemObjects();
    ThemeInvalidateDesktopWindows(Desktop);

    DEBUG(TEXT("[ResetThemeToDefault] Built-in theme reactivated"));
    return TRUE;
}

/***************************************************************************/

LPDESKTOP_THEME_RUNTIME DesktopThemeGetActiveRuntime(LPDESKTOP Desktop) {
    Desktop = ThemeResolveDesktop(Desktop);
    if (Desktop == NULL) return NULL;
    if (ThemeEnsureRuntimeState(Desktop) == FALSE) return NULL;

    return (LPDESKTOP_THEME_RUNTIME)Desktop->ActiveThemeRuntime;
}

/***************************************************************************/

BOOL DesktopThemeLookupTokenValue(LPDESKTOP Desktop, LPCSTR TokenName, LPCSTR* Value) {
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;

    if (TokenName == NULL || Value == NULL) return FALSE;

    ActiveRuntime = DesktopThemeGetActiveRuntime(Desktop);
    if (ActiveRuntime == NULL) return FALSE;

    return ThemeFindRuntimeEntry(ActiveRuntime->Tokens, ActiveRuntime->TokenCount, TokenName, Value);
}

/***************************************************************************/

BOOL DesktopThemeLookupElementPropertyValue(LPDESKTOP Desktop, LPCSTR ElementPropertyKey, LPCSTR* Value) {
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;

    if (ElementPropertyKey == NULL || Value == NULL) return FALSE;

    ActiveRuntime = DesktopThemeGetActiveRuntime(Desktop);
    if (ActiveRuntime == NULL) return FALSE;

    return ThemeFindRuntimeEntry(ActiveRuntime->ElementProperties, ActiveRuntime->ElementPropertyCount, ElementPropertyKey, Value);
}

/***************************************************************************/
