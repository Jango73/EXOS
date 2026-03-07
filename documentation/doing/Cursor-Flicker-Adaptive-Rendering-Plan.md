# Cursor Flicker and Adaptive Rendering Plan

## Purpose
Eliminate software-cursor flicker, remove redraw-on-every-mouse-move behavior, and introduce adaptive cursor pacing driven by measured render cost and FPS metrics.

## Goals
- Keep cursor ownership in kernel desktop code.
- Stop full desktop redraw on each mouse move.
- Coalesce high-frequency mouse movement into paced cursor renders.
- Measure render duration continuously and adapt cooldown dynamically.
- Maintain minimum target FPS constraints through pacing.
- Expose runtime metrics and pacing decisions through diagnostics.

## Non-Goals
- Move cursor ownership to userland.
- Rework full desktop compositor architecture in this step.
- Add external runtime dependencies.

## Core Principles
- Kernel owns cursor state, clipping, visibility, and render path.
- Mouse input updates cursor target state, not immediate full redraw.
- Rendering cadence is controlled by adaptive cooldown.
- Cooldown uses continuously updated smoothed measurements.
- Diagnostics must explain selected render pacing.

## Runtime Model
1. Mouse input updates pending cursor position.
2. Scheduler checks if cursor is dirty and cooldown expired.
3. If eligible, render one cursor frame at latest pending position.
4. Measure render time and frame interval.
5. Update smoothed metrics.
6. Recompute cooldown to respect target minimum FPS and render cost.

## Adaptive Cooldown Formula
- `FrameBudgetMs = 1000 / TargetMinFPS`
- `DesiredPeriodMs = max(FrameBudgetMs, SmoothedRenderDurationMs + SafetyMarginMs)`
- `RawCooldownMs = DesiredPeriodMs - ElapsedSinceLastPresentMs`
- `CooldownMs = clamp(MinCooldownMs, MaxCooldownMs, max(0, RawCooldownMs))`

Notes:
- EWMA smoothing is preferred for stability and low complexity.
- If `SmoothedRenderDurationMs > FrameBudgetMs`, diagnostics report budget miss.

## Implementation Steps
### Step 1: Cursor Render Stabilization
- [ ] Remove full desktop broadcast redraw trigger from mouse movement path.
- [ ] Keep one cursor dirty state with pending target position.
- [ ] Ensure software cursor draw/erase touches only bounded cursor regions.
- [ ] Verify no window overdraw regression while cursor moves.

Step gate:
- [ ] If Step 1 fully removes flicker and redraw pressure with acceptable behavior, stop here.
- [ ] In that case, all remaining steps are optional optimization work and are not required for acceptance.

### Step 2: Reusable Frame Pacing Utility (`utils`)
- [ ] Add generic `FramePacer` utility in `kernel/include/utils` + `kernel/source/utils`.
- [ ] Track last-present timestamp and instantaneous render duration.
- [ ] Compute smoothed render duration (EWMA).
- [ ] Compute instantaneous and smoothed FPS.
- [ ] Expose API for adaptive cooldown computation inputs.

### Step 3: Adaptive Cooldown Engine
- [ ] Implement cooldown computation from target FPS + smoothed render duration.
- [ ] Add bounded clamp behavior (`MinCooldownMs`, `MaxCooldownMs`).
- [ ] Add safety margin support.
- [ ] Recompute cooldown after each rendered cursor frame.
- [ ] Verify cooldown increases under load and decreases when rendering is cheap.

### Step 4: Cursor Motion Coalescing Scheduler
- [ ] On mouse move, update pending cursor coordinates without immediate render.
- [ ] Render only when dirty and cooldown has expired.
- [ ] Render latest pending position only (event coalescing).
- [ ] Clear dirty state after successful render.
- [ ] Verify render frequency is bounded under high input rate.

### Step 5: Configuration Keys
- [ ] Add `Desktop.CursorTargetMinFPS`.
- [ ] Add `Desktop.CursorCooldownMinMs`.
- [ ] Add `Desktop.CursorCooldownMaxMs`.
- [ ] Add `Desktop.CursorRenderSafetyMarginMs`.
- [ ] Add `Desktop.CursorFrameSmoothingAlpha`.
- [ ] Clamp invalid values and fall back to deterministic defaults.

### Step 6: Diagnostics and Status Reporting
- [ ] Extend `desktop status` with cursor pacing metrics:
- [ ] `cursor_path`, `cursor_size`, `cursor_cooldown_ms`.
- [ ] `cursor_render_ms_inst`, `cursor_render_ms_avg`.
- [ ] `cursor_fps_inst`, `cursor_fps_avg`, `cursor_target_min_fps`.
- [ ] Add concise `WARNING()` for sustained budget miss/fallback.
- [ ] Keep detailed pacing transitions in `DEBUG()`.

### Step 7: Validation and Hardening
- [ ] Validate `desktop show` + cursor visibility in software path.
- [ ] Validate no visible flicker during continuous mouse movement.
- [ ] Validate no full-desktop redraw per mouse event.
- [ ] Validate adaptive cooldown convergence under steady load.
- [ ] Validate x86-32 debug build.
- [ ] Validate x86-64 debug build.
- [ ] Validate hardware cursor path remains preferred when available.

## Documentation Updates
### Step 8: Final Documentation
- [ ] Update `documentation/Kernel.md` with FramePacer and adaptive cursor pacing behavior.
- [ ] Document new `Desktop.Cursor*` configuration keys and defaults.
- [ ] Document diagnostic fields exposed by `desktop status`.

## Completion Checklist
- [ ] Software cursor movement no longer causes full desktop redraw per input event.
- [ ] Flicker is removed in software cursor path under continuous movement.
- [ ] Adaptive cooldown is driven by continuously updated smoothed metrics.
- [ ] Minimum target FPS handling is implemented and observable.
- [ ] Runtime diagnostics explain pacing behavior and fallback reasons.
- [ ] x86-32 and x86-64 debug builds pass.
