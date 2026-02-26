# Intel iGPU Step 0 Compatibility Note

## Scope

This Step 0 note freezes the graphics contract needed to make Desktop and Console backend-agnostic, with:
- graphics driver ownership of glyph/text-cell rendering,
- Console as text logic only,
- one emergency fallback path: direct VGA text console.

This document records the verified current usage, then defines the target contract.

## 1) Verified current `DF_GFX_*` usage map

### Desktop path (`kernel/source/Desktop.c`)
- `ShowDesktop`:
  - uses `GetGraphicsDriver()`
  - calls `DF_GFX_SETMODE`
- `GetWindowGC`:
  - calls `DF_GFX_CREATECONTEXT`
- drawing helpers:
  - `SetPixel` -> `DF_GFX_SETPIXEL`
  - `GetPixel` -> `DF_GFX_GETPIXEL`
  - `Line` -> `DF_GFX_LINE`
  - `Rectangle` -> `DF_GFX_RECTANGLE`

### Console path (`kernel/source/Console.c`)
- Console mode changes are handled by console driver command dispatch:
  - `ConsoleSetMode` -> `ConsoleDriverCommands(DF_GFX_SETMODE, ...)`
- Console does not call selected graphics backend (`GetGraphicsDriver`) for glyph rendering.
- Console graphics-oriented commands other than mode/getmode return `DF_RETURN_NOT_IMPLEMENTED`.

### System call path (`kernel/source/SYSCall.c`)
- Graphics syscalls are routed to Desktop/windowing primitives, which then call backend `DF_GFX_*`.
- `SysCall_ConsoleSetMode` routes to `ConsoleSetMode`, not to selected graphics backend.

### Backend selection (`kernel/source/drivers/graphics/Graphics-Selector.c`)
- Active backend candidates today:
  - `IntelGfx`
  - `VESA`
- Selector forwards all known `DF_GFX_*` to the most capable active backend with fallback attempts.

## 2) Verified backend guarantees (baseline)

### VESA (`kernel/source/drivers/graphics/VESA.c`)
- Implemented:
  - `DF_LOAD`, `DF_UNLOAD`, `DF_GET_VERSION`
  - `DF_GFX_GETMODEINFO`, `DF_GFX_SETMODE`, `DF_GFX_CREATECONTEXT`
  - `DF_GFX_CREATEBRUSH`, `DF_GFX_CREATEPEN`
  - `DF_GFX_SETPIXEL`, `DF_GFX_GETPIXEL`, `DF_GFX_LINE`, `DF_GFX_RECTANGLE`
- Optional modern commands (`GETCAPABILITIES`, output/present/surface family) return `DF_RETURN_NOT_IMPLEMENTED`.

### Intel (`kernel/source/drivers/graphics/IntelGfx.c`)
- Implemented:
  - `DF_LOAD`, `DF_UNLOAD`, `DF_GET_VERSION`
  - `DF_GFX_CREATECONTEXT`
  - `DF_GFX_GETMODEINFO`, `DF_GFX_SETMODE`
  - `DF_GFX_GETCAPABILITIES`
  - `DF_GFX_SETPIXEL`, `DF_GFX_GETPIXEL`, `DF_GFX_LINE`, `DF_GFX_RECTANGLE`
  - `DF_GFX_PRESENT` (takeover path)
- Remaining optional commands return `DF_RETURN_NOT_IMPLEMENTED`.

## 3) Architecture gap identified in Step 0

Current split:
- Desktop rendering goes through selected graphics backend.
- Console rendering/mode ownership remains in a separate console-specific path.

Required direction:
- Console must no longer own graphics behavior.
- Graphics drivers must own text-cell/glyph rendering operations.
- Console becomes a pure text operation producer.

## 4) Frozen target contract (Step 0 output)

Two contracts are required.

### A) Desktop/windowing primitives contract

Mandatory:
- `DF_GFX_CREATECONTEXT`
- `DF_GFX_GETMODEINFO`
- `DF_GFX_SETMODE`
- `DF_GFX_SETPIXEL`
- `DF_GFX_GETPIXEL`
- `DF_GFX_LINE`
- `DF_GFX_RECTANGLE`

Optional:
- `DF_GFX_GETCAPABILITIES`
- `DF_GFX_ENUMOUTPUTS`
- `DF_GFX_GETOUTPUTINFO`
- `DF_GFX_PRESENT`
- `DF_GFX_WAITVBLANK`
- `DF_GFX_ALLOCSURFACE`
- `DF_GFX_FREESURFACE`
- `DF_GFX_SETSCANOUT`

Rule:
- Unimplemented optional commands return `DF_RETURN_NOT_IMPLEMENTED`.

### B) Console text-rendering contract (driver-owned text path)

New command family to add in `GFX.h`:
- `DF_GFX_TEXT_PUTCELL`
- `DF_GFX_TEXT_CLEAR_REGION`
- `DF_GFX_TEXT_SCROLL_REGION`
- `DF_GFX_TEXT_SET_CURSOR`
- `DF_GFX_TEXT_SET_CURSOR_VISIBLE`

Semantics:
- Driver receives text cell operations (character + attributes + region/cursor).
- Driver performs glyph rasterization and final framebuffer writes.
- Console does not manipulate pixels, contexts, pens, brushes, or framebuffers.

Rule:
- A backend that does not implement text commands cannot own console rendering.

## 5) Fallback policy freeze

Only one emergency fallback is accepted:
- direct VGA text console path.

It is used only if:
- no active graphics backend can satisfy required text contract, or
- backend text command dispatch fails in critical path.

No other console graphics fallback path is part of target design.

## 6) Required invariants

### Backend/context invariants
- `GRAPHICSCONTEXT.TypeID == KOID_GRAPHICSCONTEXT`
- context fields (`Width`, `Height`, `BitsPerPixel`, `BytesPerScanLine`, `MemoryBase`) match active mode
- draw commands operate on active scanout/surface consistently

### Front-end ownership invariants
- one active graphics backend at a time
- explicit front-end switch (`ConsoleFrontEnd` / `DesktopFrontEnd`)
- display switch does not require backend unload/reload to work

## 7) Step 0 deliverable status

Done:
- verified current usage map (Desktop, Console, SYSCall, selector)
- documented baseline guarantees of active backends (Intel, VESA)
- froze minimal and extended contracts for backend-agnostic Desktop + driver-owned Console text rendering
- froze emergency fallback policy to VGA text only
