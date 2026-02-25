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


    Graphics selector

\************************************************************************/

#include "GFX.h"

#include "DriverGetters.h"
#include "Log.h"

/************************************************************************/

#define GRAPHICS_SELECTOR_VER_MAJOR 1
#define GRAPHICS_SELECTOR_VER_MINOR 0

/************************************************************************/

typedef struct tag_GRAPHICS_SELECTOR_STATE {
    LPDRIVER Backends[4];
    UINT Scores[4];
    UINT BackendCount;
    UINT ActiveIndex;
} GRAPHICS_SELECTOR_STATE, *LPGRAPHICS_SELECTOR_STATE;

/************************************************************************/

static UINT GraphicsSelectorCommands(UINT Function, UINT Parameter);

static DRIVER DATA_SECTION GraphicsSelectorDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_GRAPHICS,
    .VersionMajor = GRAPHICS_SELECTOR_VER_MAJOR,
    .VersionMinor = GRAPHICS_SELECTOR_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "Graphics selector",
    .Flags = 0,
    .Command = GraphicsSelectorCommands
};

static GRAPHICS_SELECTOR_STATE DATA_SECTION GraphicsSelectorState = {0};

/************************************************************************/

/**
 * @brief Retrieve graphics selector driver descriptor.
 * @return Pointer to graphics selector driver.
 */
LPDRIVER GraphicsSelectorGetDriver(void) {
    return &GraphicsSelectorDriver;
}

/************************************************************************/

/**
 * @brief Score a graphics backend by exposed capabilities.
 * @param Driver Candidate driver.
 * @return Score value. Higher means more capable.
 */
static UINT GraphicsSelectorScoreDriver(LPDRIVER Driver) {
    GFX_CAPABILITIES Capabilities;
    UINT Score = 0;

    if (Driver == NULL || Driver->Command == NULL) {
        return 0;
    }

    if ((Driver->Flags & DRIVER_FLAG_READY) == 0) {
        return 0;
    }

    Capabilities = (GFX_CAPABILITIES){
        .Header = {.Size = sizeof(GFX_CAPABILITIES), .Version = EXOS_ABI_VERSION, .Flags = 0}
    };

    if (Driver->Command(DF_GFX_GETCAPABILITIES, (UINT)(LPVOID)&Capabilities) != DF_RETURN_SUCCESS) {
        return 1;
    }

    Score = 10;
    if (Capabilities.HasHardwareModeset) Score += 10;
    if (Capabilities.HasPageFlip) Score += 5;
    if (Capabilities.HasVBlankInterrupt) Score += 3;
    if (Capabilities.HasCursorPlane) Score += 2;
    if (Capabilities.SupportsTiledSurface) Score += 2;
    if (Capabilities.MaxWidth >= 1920 && Capabilities.MaxHeight >= 1080) Score += 1;

    return Score;
}

/************************************************************************/

/**
 * @brief Load candidate graphics backends and select the most capable active one.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_UNEXPECTED otherwise.
 */
static UINT GraphicsSelectorLoad(void) {
    LPDRIVER Candidates[2];
    UINT Index = 0;
    UINT CandidateCount = sizeof(Candidates) / sizeof(Candidates[0]);

    if ((GraphicsSelectorDriver.Flags & DRIVER_FLAG_READY) != 0) {
        return DF_RETURN_SUCCESS;
    }

    GraphicsSelectorState = (GRAPHICS_SELECTOR_STATE){0};

    Candidates[0] = IntelGfxGetDriver();
    Candidates[1] = VESAGetDriver();

    for (Index = 0; Index < CandidateCount; Index++) {
        LPDRIVER Driver = Candidates[Index];
        UINT Score = 0;
        UINT InsertIndex = 0;

        if (Driver == NULL || Driver->Command == NULL) {
            continue;
        }

        (void)Driver->Command(DF_LOAD, 0);

        if ((Driver->Flags & DRIVER_FLAG_READY) == 0) {
            DEBUG(TEXT("[GraphicsSelectorLoad] Skipping backend %s (not active)"), Driver->Product);
            continue;
        }

        Score = GraphicsSelectorScoreDriver(Driver);
        DEBUG(TEXT("[GraphicsSelectorLoad] Active backend %s score=%u"), Driver->Product, Score);

        if (GraphicsSelectorState.BackendCount >= sizeof(GraphicsSelectorState.Backends) / sizeof(GraphicsSelectorState.Backends[0])) {
            WARNING(TEXT("[GraphicsSelectorLoad] Backend table full, skipping %s"), Driver->Product);
            continue;
        }

        InsertIndex = GraphicsSelectorState.BackendCount;
        while (InsertIndex > 0 && Score > GraphicsSelectorState.Scores[InsertIndex - 1]) {
            GraphicsSelectorState.Backends[InsertIndex] = GraphicsSelectorState.Backends[InsertIndex - 1];
            GraphicsSelectorState.Scores[InsertIndex] = GraphicsSelectorState.Scores[InsertIndex - 1];
            InsertIndex--;
        }

        GraphicsSelectorState.Backends[InsertIndex] = Driver;
        GraphicsSelectorState.Scores[InsertIndex] = Score;
        GraphicsSelectorState.BackendCount++;
    }

    if (GraphicsSelectorState.BackendCount == 0) {
        ERROR(TEXT("[GraphicsSelectorLoad] No active graphics backend"));
        GraphicsSelectorDriver.Flags &= ~DRIVER_FLAG_READY;
        GraphicsSelectorState = (GRAPHICS_SELECTOR_STATE){0};
        return DF_RETURN_UNEXPECTED;
    }

    GraphicsSelectorState.ActiveIndex = 0;
    GraphicsSelectorDriver.Flags |= DRIVER_FLAG_READY;

    DEBUG(TEXT("[GraphicsSelectorLoad] Selected backend: %s (score=%u)"),
        GraphicsSelectorState.Backends[0]->Product, GraphicsSelectorState.Scores[0]);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unload graphics selector and all candidate backends.
 * @return DF_RETURN_SUCCESS always.
 */
static UINT GraphicsSelectorUnload(void) {
    LPDRIVER Candidates[2];
    UINT Index = 0;

    Candidates[0] = IntelGfxGetDriver();
    Candidates[1] = VESAGetDriver();

    for (Index = 0; Index < sizeof(Candidates) / sizeof(Candidates[0]); Index++) {
        LPDRIVER Driver = Candidates[Index];
        if (Driver == NULL || Driver->Command == NULL) {
            continue;
        }
        (void)Driver->Command(DF_UNLOAD, 0);
    }

    GraphicsSelectorState = (GRAPHICS_SELECTOR_STATE){0};
    GraphicsSelectorDriver.Flags &= ~DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Forward a command to the selected backend.
 * @param Function Command identifier.
 * @param Parameter Command parameter.
 * @return Result from selected backend.
 */
static UINT GraphicsSelectorForward(UINT Function, UINT Parameter) {
    UINT Index = 0;

    if (GraphicsSelectorState.BackendCount == 0) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    for (Index = GraphicsSelectorState.ActiveIndex; Index < GraphicsSelectorState.BackendCount; Index++) {
        LPDRIVER Driver = GraphicsSelectorState.Backends[Index];
        UINT Result = 0;

        if (Driver == NULL || Driver->Command == NULL) {
            continue;
        }

        Result = Driver->Command(Function, Parameter);

        if (Function == DF_GFX_CREATECONTEXT) {
            if (Result != 0) {
                GraphicsSelectorState.ActiveIndex = Index;
                return Result;
            }

            WARNING(TEXT("[GraphicsSelectorForward] Backend %s has no context, trying fallback"), Driver->Product);
            continue;
        }

        if (Result == DF_RETURN_NOT_IMPLEMENTED || Result == DF_RETURN_UNEXPECTED) {
            WARNING(TEXT("[GraphicsSelectorForward] Backend %s cannot handle function %x, trying fallback"),
                Driver->Product, Function);
            continue;
        }

        GraphicsSelectorState.ActiveIndex = Index;
        return Result;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Graphics selector driver entry point.
 * @param Function Driver command.
 * @param Parameter Command parameter.
 * @return Driver result.
 */
static UINT GraphicsSelectorCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return GraphicsSelectorLoad();
        case DF_UNLOAD:
            return GraphicsSelectorUnload();
        case DF_GET_VERSION:
            return MAKE_VERSION(GRAPHICS_SELECTOR_VER_MAJOR, GRAPHICS_SELECTOR_VER_MINOR);
        case DF_GFX_ENUMMODES:
        case DF_GFX_GETMODEINFO:
        case DF_GFX_SETMODE:
        case DF_GFX_CREATECONTEXT:
        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_SETPIXEL:
        case DF_GFX_GETPIXEL:
        case DF_GFX_LINE:
        case DF_GFX_RECTANGLE:
        case DF_GFX_ELLIPSE:
        case DF_GFX_GETCAPABILITIES:
        case DF_GFX_ENUMOUTPUTS:
        case DF_GFX_GETOUTPUTINFO:
        case DF_GFX_PRESENT:
        case DF_GFX_WAITVBLANK:
        case DF_GFX_ALLOCSURFACE:
        case DF_GFX_FREESURFACE:
        case DF_GFX_SETSCANOUT:
            return GraphicsSelectorForward(Function, Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
