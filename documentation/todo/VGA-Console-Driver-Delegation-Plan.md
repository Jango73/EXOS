# VGA Console Driver Delegation Plan

## Goal

Remove every direct VGA text memory access from the console subsystem.

Target architecture:
- the console remains the owner of console policy:
  - cursor position
  - regions
  - scrolling policy
  - paging
  - colors/attributes
- the VGA driver becomes the owner of VGA text output mechanics when it is the active console backend:
  - video memory writes
  - hardware cursor programming
  - clear/scroll implementations
  - mode-aware text cell output

This means `kernel/source/console/*` must no longer access:
- `0xB8000`
- VGA text memory through `Console.Memory`
- VGA CRTC cursor ports for text-mode cursor updates

## Problem Statement

The current split is inconsistent:
- the VGA driver programs modes, but does not implement text output operations
- the console writes characters, clears regions, scrolls, and updates the cursor directly against VGA text memory and VGA ports
- in graphics mode, the console already delegates text rendering to the active graphics driver through `DF_GFX_TEXT_*`
- in VGA text mode, the console bypasses the driver and owns hardware details itself

This creates two backend models:
- graphics backends are driver-owned
- VGA text backend is console-owned

That split should be removed.

## Target Design

Use one backend contract for both graphics and VGA text backends:
- the console emits backend operations
- the active backend executes them

For VGA text mode, the backend is the VGA driver.
For framebuffer mode, the backend is the active graphics driver.

The console must not know:
- where text cells are stored
- how the hardware cursor is programmed
- how scrolling is performed on a specific device

## Design Principles

- Keep policy in console, mechanics in drivers.
- Avoid introducing a second ad-hoc text-output path just for VGA.
- Reuse the existing `DF_GFX_TEXT_*` family when possible instead of inventing parallel one-off commands.
- If a command is missing for proper VGA delegation, extend the shared driver contract once and use it for all text-capable backends.
- Preserve compatibility with VGA text fallback behavior during migration.
- Keep backend selection deterministic through the existing display/console routing.

## Recommended Direction

Extend the VGA driver so it behaves like a text-capable backend, not only a mode-setting helper.

Minimum command support expected from the VGA driver:
- `DF_GFX_GETCONTEXT`
- `DF_GFX_TEXT_PUTCELL`
- `DF_GFX_TEXT_CLEAR_REGION`
- `DF_GFX_TEXT_SCROLL_REGION`
- `DF_GFX_TEXT_SET_CURSOR`
- `DF_GFX_TEXT_SET_CURSOR_VISIBLE`

Optional but desirable:
- `DF_GFX_GETCAPABILITIES`
- `DF_GFX_GETMODEINFO` for current active mode, already partly present

The console should then use the same delegation path for:
- VGA text fallback
- framebuffer text rendering

## Proposed Architecture

## 1. Introduce a text backend context model for VGA

Add a VGA-owned text context object returned by `DF_GFX_GETCONTEXT`.

Suggested responsibilities:
- expose current text mode geometry
- expose backend state needed by the VGA driver only
- keep memory base and cursor programming internal to the driver

The console must treat it like any other backend `GRAPHICSCONTEXT`.

## 2. Move all VGA text hardware operations into the VGA driver

Move from console to VGA driver:
- write one text cell
- fill/clear one region
- scroll one region
- set cursor position
- show/hide cursor

This includes:
- writes to VGA text memory
- writes to VGA cursor registers
- any mode-specific stride/geometry logic

## 3. Make the console use backend commands only

Refactor the console so that text output goes through one dispatch path:
- acquire active backend driver
- acquire backend context
- send text commands

No direct hardware branch in console code.

In particular, remove logic branches based on:
- `Console.UseFramebuffer` when the branch exists only to choose between direct VGA memory access and driver calls
- `Console.Memory` as a direct text buffer pointer

The console may still keep a high-level mode flag if needed for layout or policy, but not for hardware access.

## 4. Keep mode activation separate from text output

`ConsoleVGATextFallbackActivate()` should remain responsible for selecting VGA fallback mode, but after activation:
- output must still flow through driver commands
- no direct writes to VGA text memory should occur in console code

So the fallback function should:
- activate VGA mode
- select VGA as the active text backend
- reset console policy state
- not perform hardware rendering itself

## Migration Plan

## [x] Step 1 - Define the backend contract baseline

- Audit all text operations currently performed directly by the console in VGA mode.
- Map each operation to an existing `DF_GFX_TEXT_*` command.
- If one operation has no suitable command, extend the shared backend contract once in `GFX.h` and update all relevant drivers.

Success criteria:
- there is one explicit command-level contract for all console text output operations.

## [x] Step 2 - Add VGA text context and command handlers

- Implement `DF_GFX_GETCONTEXT` in the VGA driver.
- Implement `DF_GFX_TEXT_PUTCELL`.
- Implement `DF_GFX_TEXT_CLEAR_REGION`.
- Implement `DF_GFX_TEXT_SCROLL_REGION`.
- Implement `DF_GFX_TEXT_SET_CURSOR`.
- Implement `DF_GFX_TEXT_SET_CURSOR_VISIBLE`.

Implementation note:
- the VGA driver should internally own the text memory base and cursor port programming.
- the console must not receive raw VGA memory pointers.

Success criteria:
- the VGA driver is sufficient to render and update a text console without help from direct console-side hardware access.

## [x] Step 3 - Refactor console write path

- Replace direct `Console.Memory[...]` writes with backend command dispatch.
- Route single-character output, line output, clear, backspace erase, and scrolling through the backend abstraction.
- Route cursor movement through backend cursor commands.

Success criteria:
- no text output path in `Console-Main.c` writes directly to VGA text memory.

## [x] Step 4 - Remove direct VGA cursor programming from console

- Remove console-side writes to VGA CRTC cursor registers.
- Use backend cursor commands for both position and visibility.
- Keep any cursor policy in console, but backend execution in the driver.

Success criteria:
- the console never programs VGA cursor hardware directly.

## [x] Step 5 - Remove console ownership of VGA text memory

- Remove `Console.Memory` as a hardware text-buffer access mechanism.
- If a console buffer pointer is still needed for non-VGA internal purposes, rename it so it does not imply hardware ownership.
- Remove all remaining `0xB8000` references from console sources.

Success criteria:
- `kernel/source/console/*` contains no `0xB8000` and no direct VGA text memory writes.

## [x] Step 6 - Unify backend acquisition

- Make console backend acquisition work uniformly for:
  - VGA text backend
  - GOP
  - VESA
  - iGPU
- Avoid a special hidden branch where VGA bypasses `Console-TextOps.c`.

Success criteria:
- one code path selects the active backend and dispatches text operations.

## [x] Step 7 - Validate all output behaviors

Validate at minimum:
- normal character output
- line wrapping
- newline handling
- backspace handling
- clear screen
- region clear
- scroll
- cursor move
- cursor visibility
- fallback activation to 80x25 and requested VGA text modes

Success criteria:
- behavior matches current console output semantics with no direct console hardware access.

## Refactoring Notes

## Console responsibilities to keep

- text flow policy
- region policy
- paging
- debug split policy
- color/attribute selection
- cursor logical position
- mode/layout bookkeeping visible to higher-level console users

## VGA driver responsibilities to gain

- text cell storage format
- physical/linear VGA text memory access
- mode-specific row stride and bounds
- hardware cursor programming
- efficient scroll/clear implementation

## Risks

- If backend commands are underspecified, VGA may need driver-private exceptions. Avoid that by fixing the shared contract first.
- If the console keeps dual paths during migration too long, regressions will hide in the fallback path. Prefer short-lived transition layers.
- Region scrolling semantics must remain defined at console level even if implementation is delegated.

## Explicit Non-Goal

This plan does not move console policy into the VGA driver.

The target is not "make VGA own the console".
The target is "make VGA own VGA hardware operations".

## Acceptance Checklist

- `kernel/source/console/*` has no direct `0xB8000` access.
- `kernel/source/console/*` has no direct VGA cursor port programming.
- VGA text output works through driver commands.
- Console text output uses backend delegation in both VGA text mode and graphics mode.
- `documentation/Kernel.md` is updated when implementation lands.
