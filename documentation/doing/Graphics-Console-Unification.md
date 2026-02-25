# Graphics Console Unification Plan

## Goal

Use one graphics backend pipeline for both Desktop and Console.

Expected behavior:
- UEFI boot uses a graphics driver backend (GOP first, then more capable native drivers when available).
- Console rendering and Desktop rendering both target the same selected graphics backend.
- Entering/leaving Desktop mode does not rely on driver unload side effects.
- Glyph rasterization and text-cell rendering are responsibilities of the active graphics driver.
- Console code contains no graphics concepts.

## Problem Statement

The system has separate rendering paths:
- Desktop path goes through `GetGraphicsDriver()` and graphics drivers.
- Console path uses console-specific rendering paths (text mode or direct boot framebuffer path).

This split creates fragile mode transitions and makes return-to-console behavior backend-dependent.

## Target Architecture

1. Keep one `GraphicsSelector` entry point.
2. Register all graphics backends under that selector:
   - `IntelGfx` (or other native backends)
   - `VESA` compatibility backend
   - `GOP` backend for UEFI boot path
3. Make graphics drivers responsible for text rendering:
   - Graphics driver exposes text-cell rendering commands (glyph draw, region scroll, cursor draw).
   - Console emits text operations only (characters, attributes, cursor, regions), never pixels.
   - Console no longer depends on framebuffer ownership rules.
4. Introduce explicit display state switching:
   - `ConsoleFrontEnd` active
   - `DesktopFrontEnd` active
   - both front-ends share the same backend and present model
5. Keep one strict emergency fallback:
   - if no graphics backend can expose a valid context, switch to direct `B800` text console.

## Step-by-Step Implementation

## Step 0 - Contract freeze for front-end/back-end boundary

- Define two explicit contracts:
  - graphics primitives contract for Desktop/windowing
  - text rendering contract for Console
- Text contract must include:
  - draw text cell with foreground/background attributes
  - scroll text region
  - clear text region
  - show/hide/move cursor
- Define a fallback policy for drivers that do not implement text rendering:
  - selector rejects them for console ownership and keeps currently active backend, or
  - system enters emergency `B800` text console mode.
- Console must not import or use graphics context structures.

Define a minimal shared contract used by both Desktop and Console orchestration:
  - context creation
  - mode info query
  - present
  - optional surface allocation
- Document mandatory vs optional driver functions.
- Keep all optional commands returning `DF_RETURN_NOT_IMPLEMENTED` when missing.

Deliverable:
- Compatibility note in `documentation/doing` describing mandatory driver commands for shared Console/Desktop rendering.

## Step 1 - Add GOP backend as a first-class graphics driver

- Add a dedicated `GOP` graphics backend module in kernel graphics drivers.
- Load it only when boot environment exposes GOP/UEFI framebuffer metadata.
- Implement:
  - `DF_LOAD` / `DF_UNLOAD`
  - `DF_GFX_GETMODEINFO`
  - `DF_GFX_SETMODE` (use GOP mode set where available, or keep current mode when setmode is unsupported)
  - `DF_GFX_CREATECONTEXT`
  - CPU drawing primitives needed by existing API (`SETPIXEL`, `LINE`, `RECTANGLE`)
  - `DF_GFX_GETCAPABILITIES`

Deliverable:
- UEFI machines can run Desktop rendering through GOP backend without relying on direct boot framebuffer console rendering.

## Step 2 - Strengthen selector policy for startup ownership

- Extend selector policy:
  - choose best capable active backend.
  - enforce deterministic priority rules.
- Add a capability score function:
  - native modeset support
  - present model quality
  - vblank support
  - output management support
- Keep startup logs concise:
  - selected backend
  - capability score
  - rejected backends and reason codes

Deliverable:
- Backend selection remains deterministic and explainable on every boot.

## Step 3 - Introduce display session abstraction

- Add a kernel-level display session object that owns:
  - selected backend
  - active mode
  - active front-end (Console or Desktop)
  - optional shared surface metadata
- Remove direct mode ownership assumptions from individual callers.
- Route Desktop and Console switch through this display session API.

Deliverable:
- Mode transitions become explicit state transitions, not implicit side effects.

## Step 4 - Move console rendering responsibility into graphics drivers

- Add text rendering driver commands in `GFX.h` (for example `DF_GFX_TEXT_*` family):
  - write cell
  - clear region
  - scroll region
  - cursor control
- Implement these commands in:
  - GOP backend
  - Intel backend
  - VESA backend (minimal CPU implementation)
- Keep the same Console API externally (`ConsolePrint`, `ClearConsole`, etc.), but internally:
  - Console emits text operations to display session.
  - Display session dispatches to active graphics driver text commands.
- Remove pixel/glyph drawing logic from Console modules.

Deliverable:
- Console and Desktop share the same backend ownership while Console remains text-only logic.

## Step 5 - Add explicit front-end switch command path

- Add kernel helpers:
  - `DisplaySwitchToConsole()`
  - `DisplaySwitchToDesktop(LPDESKTOP Desktop)`
- Ensure both helpers:
  - keep backend loaded
  - keep mode ownership coherent
  - trigger redraw of active front-end
- Stop using `DF_UNLOAD` as a display switch mechanism.

Deliverable:
- Commands like `gfx_smoke` can enter desktop mode and return to console without backend teardown.

## Step 6 - Keep emergency `B800` fallback without leaking graphics concerns into Console

- Keep direct `B800` text mode support isolated in one fallback module.
- Use it only when:
  - no graphics backend with text contract is active
  - text command dispatch fails during early bring-up
  - catastrophic graphics bring-up error occurs
- Add clear warning logs when fallback is entered.

Deliverable:
- Stable fallback exists without weakening primary unified path.

## Step 7 - Test and acceptance gates

- Boot tests:
  - x86-64 UEFI with GOP backend
  - x86-64/x86-32 BIOS path with selector fallback
- Functional tests:
  - console output immediately after boot
  - switch to desktop and back to console repeatedly
  - shell command graphics smoke path
  - window invalidation stress for 15 seconds
- Failure-path tests:
  - force backend reject to validate fallback
  - force `DF_GFX_CREATECONTEXT` failure and verify controlled fallback

Acceptance criteria:
- No fault/panic in logs during switch loops.
- Console remains readable and interactive after each Desktop return.
- Selector always reports one active path (graphics or explicit `B800` emergency fallback).

## Suggested File Split

- `kernel/source/drivers/graphics/GOP.c`
- `kernel/source/drivers/graphics/Graphics-Selector.c` (policy/scoring updates)
- `kernel/source/display/DisplaySession.c` (new shared ownership layer)
- `kernel/source/drivers/graphics/*` (text command implementations per backend)
- `kernel/source/console/Console-TextOps.c` (text operation emission only, no pixel logic)
- `kernel/source/console/Console-B800Fallback.c` (emergency text-only fallback path)

## Documentation updates required during implementation

- Update `documentation/Kernel.md`:
  - graphics selector policy
  - display session ownership model
  - console rendering path through graphics backend
- Add doxygen headers for new display/console modules.
