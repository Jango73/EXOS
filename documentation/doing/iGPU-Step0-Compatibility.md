# Intel iGPU Step 0 Compatibility Note

## 1) Effective `DF_GFX_*` usage map

### Desktop (`kernel/source/Desktop.c`)
- Direct driver calls:
  - `DF_GFX_SETMODE` in `ShowDesktop`.
  - `DF_GFX_SETPIXEL` in `SetPixel`.
  - `DF_GFX_GETPIXEL` in `GetPixel`.
  - `DF_GFX_LINE` in `Line`.
  - `DF_GFX_RECTANGLE` in `Rectangle`.

### Console (`kernel/source/Console.c`)
- Effective graphics driver command usage:
  - `DF_GFX_SETMODE` via `ConsoleSetMode`.
- Console driver command handler exposes:
  - `DF_GFX_GETMODEINFO`
  - `DF_GFX_SETMODE`
  - other `DF_GFX_*` return `DF_RETURN_NOT_IMPLEMENTED`.

### System calls (`kernel/source/SYSCall.c`)
- No direct `Driver->Command(DF_GFX_...)` call.
- Indirect usage through Desktop graphics helpers:
  - `SysCall_SetPixel` -> `SetPixel` -> `DF_GFX_SETPIXEL`
  - `SysCall_GetPixel` -> `GetPixel` -> `DF_GFX_GETPIXEL`
  - `SysCall_Line` -> `Line` -> `DF_GFX_LINE`
  - `SysCall_Rectangle` -> `Rectangle` -> `DF_GFX_RECTANGLE`
  - `SysCall_ConsoleSetMode` -> `ConsoleSetMode` -> `DF_GFX_SETMODE`

## 2) What `VESA.c` guarantees

### Implemented commands
- `DF_LOAD` / `DF_UNLOAD` / `DF_GET_VERSION`
- `DF_GFX_GETMODEINFO`
- `DF_GFX_SETMODE`
- `DF_GFX_CREATEBRUSH`
- `DF_GFX_CREATEPEN`
- `DF_GFX_SETPIXEL`
- `DF_GFX_GETPIXEL`
- `DF_GFX_LINE`
- `DF_GFX_RECTANGLE`

Any other command returns `DF_RETURN_NOT_IMPLEMENTED`.

### Mode set and framebuffer guarantees
- `DF_GFX_SETMODE` selects a mode from the internal `VESAModeSpecs` table.
- The mode path requires linear framebuffer support (`Attributes & 0x80`).
- Physical framebuffer is mapped with `MapIOMemory`, then exposed through `GRAPHICSCONTEXT.MemoryBase`.
- On success, context fields are updated:
  - `Width`, `Height`, `BitsPerPixel`, `BytesPerScanLine`, `MemoryBase`
  - clip rectangle (`LoClip`, `HiClip`)
- Old mappings are released before remapping (`UnMapIOMemory`).

### Drawing behavior guarantees
- Pixel primitives are implemented for 8/16/24 bits per pixel.
- `LINE` and `RECTANGLE` are implemented for 16/24 bits per pixel.
- 8 bits per pixel line/rectangle paths are stubs (no rendering guarantee).
- `SETPIXEL`, `GETPIXEL`, `RECTANGLE` use context mutex protection.
- `LINE` path has no active mutex lock in `VESA.c`.

## 3) Minimal backend contract for the window manager

To run the existing window manager path without backend-specific code, each graphics backend must provide:

### Required commands
- `DF_GFX_SETMODE`
- `DF_GFX_GETMODEINFO`
- `DF_GFX_SETPIXEL`
- `DF_GFX_GETPIXEL`
- `DF_GFX_LINE`
- `DF_GFX_RECTANGLE`

### Required command semantics
- `DF_GFX_SETMODE`:
  - validates requested mode.
  - activates scanout/framebuffer.
  - updates the active `GRAPHICSCONTEXT` fields used by Desktop.
- `DF_GFX_GETMODEINFO`:
  - returns the active mode values matching the context.
- Drawing commands:
  - accept `PIXELINFO`, `LINEINFO`, `RECTINFO`.
  - use the `GC` handle in payload.
  - render into the active framebuffer referenced by the context.
  - tolerate coordinates after Desktop-origin translation.

### Required context invariants
- `GRAPHICSCONTEXT.TypeID == KOID_GRAPHICSCONTEXT`
- `GRAPHICSCONTEXT.Driver` points to the backend driver.
- `MemoryBase != NULL` after successful graphics mode set.
- `BytesPerScanLine` is consistent with active scanout format.
- `LoClip/HiClip` bound valid drawable range.

## 4) Architecture coupling to remove for backend-agnostic design

`GetWindowGC` in `Desktop.c` binds directly to `VESAContext`, and `GetGraphicsDriver` returns `VESAGetDriver()` unconditionally.  
For true backend-agnostic behavior, context acquisition must be routed through the selected graphics backend rather than hardcoded VESA symbols.
