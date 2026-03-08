# Desktop Clock Widget + Window Timer Plan

## Goal

Implement a kernel desktop analog clock widget with reusable rendering primitives and a generic window timer mechanism.

Scope:
- New clock widget module with its own `WindowFunc`.
- Instantiate the clock widget instead of the second internal test window on `desktop show`.
- Add `Arc` and `Triangle` drawing primitives to VESA backend and desktop graphics API.
- Add a generic per-window timer mechanism (`SetTimer` / `KillTimer` style) and `EWM_TIMER` delivery.

## Constraints

- No ad-hoc one-off logic in desktop code for timing.
- Keep kernel/user separation clean (`SetWindowProp` is not used as kernel internal state storage for this feature).
- Keep `DefWindowFunc` as generic behavior owner.
- Keep style and type rules from `AGENTS.md` (`TEXT(...)`, Base types, no libc).

## Step 1 - Add Generic Window Timer Mechanism

### 1.1 API and message contract
- Add `EWM_TIMER` message identifier (if absent).
- Add desktop timer APIs:
  - `BOOL SetWindowTimer(HANDLE Window, U32 TimerID, U32 IntervalMilliseconds);`
  - `BOOL KillWindowTimer(HANDLE Window, U32 TimerID);`
- Timer message payload:
  - `Param1 = TimerID`
  - `Param2 = 0`

### 1.2 Internal architecture
- Create reusable desktop timer module near rendering engine (not utils):
  - `kernel/include/desktop/Desktop-Timer.h`
  - `kernel/source/desktop/Desktop-Timer.c`
- Maintain one bounded timer table per desktop.
- Timer tick source: dispatcher loop checks due timers each wake cycle.
- On due timer:
  - rearm periodic timer,
  - `PostMessage(Window, EWM_TIMER, TimerID, 0)`.

### 1.3 Lifetime and safety rules
- Auto-remove timers when target window is destroyed.
- Ignore invalid/non-visible targets safely.
- Keep deterministic bounded complexity per dispatcher iteration.

## Step 2 - Add Arc and Triangle Primitives

### 2.1 Graphics contract
- Extend graphics command contract with:
  - `DF_GFX_ARC`
  - `DF_GFX_TRIANGLE`
- Add matching info structs (ABI-safe header + GC + geometry fields):
  - `ARCINFO`
  - `TRIANGLEINFO`

### 2.2 Desktop graphics wrappers
- Add kernel drawing wrappers:
  - `BOOL Arc(LPARCINFO Info);`
  - `BOOL Triangle(LPTRIANGLEINFO Info);`
- Expose in desktop public header where other primitives live.

### 2.3 VESA implementation
- Implement in `kernel/source/drivers/graphics/VESA-Primitives.c`.
- Respect clip rectangle and current pen/brush model.
- Arc:
  - circle/arc rasterization with clip-aware per-pixel/per-segment write.
- Triangle:
  - filled triangle using scanline rasterization (brush),
  - optional border using pen when enabled.

## Step 3 - Create Clock Widget Module

### 3.1 New module
- Add:
  - `kernel/include/desktop/Desktop-ClockWidget.h`
  - `kernel/source/desktop/Desktop-ClockWidget.c`
- Expose one creation helper:
  - `BOOL DesktopClockWidgetEnsureVisible(LPDESKTOP Desktop);`

### 3.2 Window procedure behavior
- `EWM_CREATE`:
  - register a 1000 ms timer (`TimerID = 1`).
- `EWM_TIMER` (`TimerID == 1`):
  - request redraw for this widget.
- `EWM_DRAW`:
  - draw clock body:
    - black circle with 2 px thickness (outer + inner arc/ring strategy),
    - hour/minute/second hands.
- `EWM_DELETE`:
  - kill timer.

### 3.3 Time source
- Use existing kernel time API used elsewhere in desktop.
- Convert local time to analog hand angles:
  - hour: `(hour % 12 + minute / 60.0) * 30°`
  - minute: `(minute + second / 60.0) * 6°`
  - second: `second * 6°`

## Step 4 - Integrate into `desktop show`

### 4.1 Internal test desktop setup
- Update internal test window bootstrap:
  - keep first test window,
  - replace second test window creation with `DesktopClockWidgetEnsureVisible`.
- Keep deterministic position/size in desktop coordinates.

### 4.2 Remove duplication
- Keep common geometry conversion in `Graphics-Utils` where relevant.
- No duplicated titlebar/hittest logic in widget code.

## Step 5 - Validation Plan

### 5.1 Build checks
- `bash scripts/build.sh --arch x86-32 --fs ext2 --debug`
- `bash scripts/build.sh --arch x86-64 --fs ext2 --debug`

### 5.2 Runtime checks
- `desktop show`:
  - clock window appears in place of second test window,
  - second hand updates once per second.
- Drag/move still works for normal windows.
- No freeze when clicking title bar.
- No cursor trail regression.

### 5.3 Rendering checks
- Arc and triangle clip correctly at window edges.
- Circle border remains exactly 2 px thickness.
- Hands are stable and centered.

## Step 6 - Documentation Updates

- Update `documentation/Kernel.md`:
  - task/window messaging section with timer behavior,
  - desktop internals section with clock widget integration,
  - graphics backend section with arc/triangle primitives.

## Delivery Order

1. Timer mechanism (`EWM_TIMER`, set/kill APIs, dispatcher integration).
2. Arc/Triangle primitive contract + VESA implementation.
3. Clock widget module (`WindowFunc`, rendering, timer usage).
4. Replace second internal test window on `desktop show`.
5. Full x86-32/x86-64 validation and final doc update.
