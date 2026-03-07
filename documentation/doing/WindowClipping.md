# Window Clipping and Dirty Redraw Plan

## Purpose
Define an incremental implementation plan for dirty-rectangle tracking and clip-driven redraw in the kernel windowing pipeline.

## Scope
- Kernel windowing and compositor-side redraw pipeline.
- Desktop and window invalidation flow.
- Clip usage through existing graphics context fields (`LoClip` / `HiClip`).
- No ABI break for userland window procedures.

## Baseline Notes (Step 1)
- `InvalidateWindowRect` updates a single per-window `InvalidRect` and posts `EWM_DRAW`.
- `DefWindowFunc` handles default redraw path (`EWM_DRAW`) and non-client painting.
- Primitive drawing goes through graphics contexts, and backends already honor `LoClip` / `HiClip`.
- `MoveWindow` default behavior is still a stub and is addressed in a later step.

## Implementation Steps
### Step 1: Baseline and Safety Fixes
- [x] Audit and document current redraw flow (`InvalidateWindowRect`, `EWM_DRAW`, `DefWindowFunc`, graphics primitives).
- [x] Fix lock/re-entrancy risks in invalidation helpers before extending the pipeline.
- [x] Add focused debug counters/log points for redraw diagnostics (rate-limited).

### Step 2: Add Reusable Rect Region Utility
- [x] Create a generic region module in `kernel/include/utils` + `kernel/source/utils`.
- [x] Implement rectangle-region primitives: init, reset, add, union, intersect, iterate.
- [x] Add bounded capacity and deterministic fallback behavior when limits are exceeded.

### Step 3: Replace Single Invalid Rectangle with Dirty Region
- [x] Replace single `InvalidRect` accumulation semantics with region-based dirty tracking.
- [x] Keep `InvalidateWindowRect` API behavior compatible for existing callers.
- [x] Ensure draw message coalescing still works with region accumulation.

### Step 4: Clip-Driven Redraw Pipeline
- [x] Build a per-frame compiled clip region from accumulated dirty rectangles.
- [x] Apply rectangle merge/coalescing rules to keep clip complexity bounded.
- [x] Define fallback path to full redraw when clip fragmentation exceeds limits.
- [x] Update redraw path to iterate clip rectangles and paint only intersected areas.
- [x] Drive drawing through existing `GRAPHICSCONTEXT` clip fields (`LoClip`, `HiClip`) per clip rectangle.
- [x] Keep primitive rendering backend-agnostic (VESA, GOP, iGPU).

### Step 5: Implement Window Move Damage in Default Procedure
- [x] Extend `DefWindowFunc` to handle move-related default behavior.
- [x] On move, invalidate old window bounds and new window bounds.
- [x] Trigger redraw through dirty region pipeline without forcing full-screen redraw.

### Step 6: Tile-Indexed Clip Traversal for Glyph-Heavy Workloads
- [ ] Add a reusable tile grid helper to index clip/damage rectangles by screen-space cells.
- [ ] Map compiled dirty/clip rectangles into touched cells with bounded storage and deterministic overflow fallback.
- [ ] Use preallocated bounded storage for tile-cell index entries (no per-entry `KernelHeapAlloc` in redraw paths), with reset-per-frame reuse.
- [ ] Add fast lookup helpers so render paths query only rectangles attached to current tile(s) instead of scanning the full region.
- [ ] Integrate tile-indexed lookups in glyph/text-heavy draw paths while preserving existing clip correctness.
- [ ] Add `exos.*.toml` configuration keys for tile size and dirty-rectangle count threshold that switches redraw to tile-indexed traversal, with strict parsing and safe defaults.
- [ ] Resolve tile-index settings at desktop activation and apply them without ABI changes.
- [ ] Keep non-tile fallback path active for low-complexity scenes and overflow cases.

### Step 7: Compatibility and Validation
- [ ] Keep explicit `DefWindowFunc` fallback model unchanged (window procedure decides).
- [ ] Preserve syscall/runtime behavior and existing windowing ABI.
- [ ] Validate that userland and kernel windows use the same clipping/invalidation semantics.
- [ ] Verify x86-32 and x86-64 builds.
- [ ] Validate behavior in at least VESA and one hardware-native graphics backend (GOP or iGPU).
- [ ] Confirm `desktop show` path produces visible move/redraw updates with clipping enabled.
- [ ] Add targeted tests for overlapping dirty rectangles and clip merges.
- [ ] Add tests for move stress, redraw ordering, and no-regression on non-client rendering.
- [ ] Add fallback/limit tests (region capacity exceeded, forced full redraw).
