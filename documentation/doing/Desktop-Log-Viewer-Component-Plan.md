# Desktop Log Viewer Component Plan

## Scope
- Add one new component in `kernel/source/ui` that displays the kernel log inside the desktop UI.
- Follow the same component pattern as existing UI components (`ClockWidget`, `OnScreenDebugInfo`).
- Keep the integration strictly on high-level APIs.
- Do not read or mutate `desktop/` internals from the component.
- Refresh the view periodically every 500 ms, but redraw only when the log content changed.
- Keep the visible content auto-scrolled to the latest lines.
- Host the component in one floating desktop window, not inside the shell bar.

## Non-Goals
- No direct access from the component to `desktop` private structures, mutexes, or state snapshots outside public APIs.
- No direct tap into serial output, console internals, or raw driver output from the component.
- No ad-hoc local rolling buffer implementation inside the component if shared buffer helpers can be reused.
- No manual user scroll behavior in the first milestone.
- No code changes in this step, only the implementation plan.

## Existing Constraints Observed
- UI components live in `kernel/source/ui` and expose public entry points in `kernel/include/ui`.
- Desktop-owned component injection is centralized in `kernel/source/desktop/Desktop-Components.c`.
- Existing components rely on high-level window APIs such as:
  - `CreateWindow`
  - `SetWindowTimer`
  - `InvalidateWindowRect`
  - `BeginWindowDraw`
  - `DrawText`
- `Log.h` does not expose any API to snapshot or consume recent log lines.
- A generic reusable `CircularBuffer` already exists in:
  - `kernel/include/utils/CircularBuffer.h`
  - `kernel/source/utils/CircularBuffer.c`

## Design Rules
- The component must consume the log through one public `Log` API.
- The log storage mechanics must stay owned by the `Log` module, not by the UI component.
- If rolling byte storage is needed, use the shared `CircularBuffer` helper or extend it cleanly if one missing operation is required.
- The component must remain dumb:
  - poll one log snapshot API,
  - keep one local render cache,
  - invalidate itself only when the source sequence changed.
- Any concurrency protection for log data must stay inside the `Log` module.
- The component must never lock another subsystem object directly.

## Proposed Architecture

### 1. New public log view API
Extend `kernel/include/Log.h` with one read-only high-level snapshot API dedicated to recent log text.

Suggested shape:
- one `KERNEL_LOG_VIEW` or `KERNEL_LOG_SNAPSHOT` structure
- one function such as `KernelLogCaptureRecent(...)`

Responsibilities of this API:
- return recent log text already assembled as lines,
- expose one monotonic change counter or sequence number,
- hide all internal storage details,
- guarantee a stable snapshot for one caller without exposing internal pointers.

The UI component will only use this API.

### 2. Log-owned rolling storage
Add recent-log retention inside `kernel/source/Log.c`.

The clean source of truth is:
- `KernelLogText(...)` continues to emit to serial/console as before,
- the same formatted line is also appended to one in-memory recent-log store,
- the store keeps only the latest retained bytes/lines.

This storage belongs in `Log`, not in `ui`, because:
- the log producer already lives there,
- retention policy is log policy,
- it avoids duplicating formatting and synchronization paths,
- it gives one reusable API for future tools beyond the desktop component.

### 3. UI component
Add one new component pair:
- `kernel/include/ui/LogViewer.h`
- `kernel/source/ui/LogViewer.c`

Behavior:
- register one kernel window class like the clock widget does,
- start one 500 ms timer on `EWM_CREATE`,
- on each tick, ask `Log` for the latest snapshot metadata,
- if the change counter did not move, do nothing,
- if it changed, copy the new snapshot into local render state and invalidate the window,
- on draw, render only the lines that fit in the client area, anchored to the newest line.

### 4. Desktop integration
Wire the component in `kernel/source/desktop/Desktop-Components.c` as one dedicated floating child window of the desktop root.

This keeps the layering intact:
- `desktop` decides composition,
- `ui` provides reusable component classes,
- `Log` provides data through a public API.

## Rolling Buffer Sizing

### Target
- Retain about 500 lines.

### Byte estimate
Reasonable sizing for kernel log lines:
- average useful line length: about 140 to 160 bytes
- newline and terminator budget: about 2 bytes
- per-line metadata budget if needed for offsets/length/sequence: about 8 to 12 bytes

Estimate with 160-byte average payload:
- text payload: `500 * 160 = 80000` bytes
- metadata: about `500 * 12 = 6000` bytes
- margin for longer lines and temporary formatting slack: about `10000` bytes

Rounded target:
- about `96000` bytes
- practical fixed budget: `96 KiB`

This is the size I would target for the first implementation.

If the existing `CircularBuffer` is used only for raw bytes, I would still keep a small side table for line starts/counts so snapshot extraction stays cheap.

## Auto-Scroll Policy
- First version is always pinned to the latest available lines.
- The component does not preserve an older viewport position.
- On each redraw, it computes how many lines fit vertically and renders the last `N` lines.
- This satisfies the requested automatic scroll behavior without adding manual scroll state yet.

## Refresh Policy
- Timer interval: `500` ms.
- The timer does not force a redraw every tick.
- The component compares the last seen log sequence with the current sequence.
- It invalidates only when new data arrived.

This keeps the component simple and avoids useless redraw churn.

## Concurrency Model
- Writers stay in `KernelLogText(...)`.
- The `Log` module owns synchronization around its recent-log store.
- The snapshot API returns one copied snapshot or fills a caller-provided buffer.
- The UI component never touches internal log buffers directly.

Preferred flow:
1. `Log` locks its own state.
2. `Log` copies recent text and metadata into caller-owned output.
3. `Log` unlocks.
4. The component renders from its local copy.

This matches the repository rule that owner-side helpers expose snapshots instead of external code locking internals.

## Data Shape I Intend To Use

### Inside `Log`
- one byte rolling buffer for recent formatted text
- one retained line count
- one monotonic sequence counter incremented when a complete line is appended
- optional line offset ring to avoid rescanning from the beginning on every snapshot

### Inside the UI component
- one local text buffer containing the last captured snapshot
- one local line offset array for rendering
- one last-seen sequence value
- one cached line height from `MeasureText`

The component buffer is render cache only, not the source of truth.

## Implementation Steps

## Step 1 - Freeze the public contract
- Add one log snapshot structure in `Log.h`.
- Add one public function to capture recent retained log content.
- Keep the contract read-only and desktop-agnostic.

Deliverable:
- UI code can ask for recent log lines through `Log.h` only.

## Step 2 - Add recent-log retention in `Log.c`
- Reuse `utils/CircularBuffer` for retained bytes if it fits.
- Add one bounded recent-log policy targeting about `96 KiB`.
- Append formatted lines after the existing emit path.
- Track one monotonic update sequence.

Deliverable:
- `Log` can provide a stable snapshot of recent lines.

## Step 3 - Add the new UI component
- Create `LogViewer.h` and `LogViewer.c`.
- Register one window class.
- Start and stop the 500 ms timer in create/delete.
- Maintain one local render cache.
- Render the latest visible lines using existing text drawing APIs.

Deliverable:
- One standalone UI component that behaves like the existing ones.

## Step 4 - Inject it through desktop composition
- Extend `Desktop-Components.c` with one injection path for the log viewer.
- Create it directly as its own floating child window under the desktop root using the same high-level window APIs.
- Give it an explicit initial rectangle sized for readable log output.
- Keep the creation path separate from shell bar composition.

Deliverable:
- The desktop owns the component through standard composition paths.

## Step 5 - Validate behavior
- Verify no direct dependency from `ui/LogViewer.c` to `desktop` private files.
- Verify the component redraws only when the log sequence changes.
- Verify the latest lines remain visible after repeated updates.
- Verify the bounded retention stays near the expected memory budget.
- Verify no faults on x86-32 and x86-64 when log traffic is high.

Deliverable:
- Component is wired cleanly and behaves predictably under sustained logging.

## Placement Note
The log viewer is a dedicated floating desktop window.

The data path remains the same:
- `ui/LogViewer` consumes `Log.h`
- `Desktop-Components` performs composition
- no component code reaches into `desktop/` internals
- no shell bar integration is involved
