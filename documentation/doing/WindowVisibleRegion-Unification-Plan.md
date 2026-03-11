# Window Visible Region Unification Plan

## Goal

Unify visible-region construction behind one shared entry point without changing the redraw pipeline, message flow, or draw scheduling.

The cursor path is the reference behavior because it already computes visibility correctly in practice:

- it starts from one base screen rectangle
- it subtracts visible sibling subtrees that are in front of the target window
- it subtracts the target window children when rendering an overlay on that window
- it returns one `RECT_REGION` consumed by the existing drawing code

The window redraw path and the root redraw path must use the same region builder rules.

## Non-goals

- no desktop-wide damage-region redesign
- no redraw pipeline replacement
- no new drag-specific invalidation path
- no new permanent clip state on `WINDOW`
- no fallback to one union clip rectangle as source of truth

## Reference Path

The reference implementation is in:

- [Desktop-Cursor.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Cursor.c)

The key function is:

- `DesktopCursorBuildVisibleRegionForWindow()`

The supporting subtree subtraction logic is:

- `DesktopCursorSubtractVisibleWindowTreeFromRegion()`

This behavior must become the canonical visible-region builder for window-relative draws.

## Problem Summary

The codebase still has multiple region builders with duplicated logic:

- cursor overlay path in [Desktop-Cursor.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Cursor.c)
- window draw path in [Desktop-Graphics.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Graphics.c)
- root draw path in [Desktop-Graphics.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Graphics.c)

These builders are close, but not identical. That is enough to create inconsistent results during:

- first desktop draw
- floating-window redraw
- cursor-triggered redraw
- drag redraw over `OnScreenDebugInfo`

The pipeline then executes correctly on top of inconsistent input regions.

## Structural Change

Create one shared visible-region module for desktop/window visibility computation.

Suggested files:

- `kernel/include/utils/VisibleRegion.h`
- `kernel/source/utils/VisibleRegion.c`

Reason:

- the operation is generic region construction logic
- it is not specific to cursor rendering
- it is not specific to one desktop source file
- it matches the repository rule that reusable mechanics belong in `kernel/include/utils` and `kernel/source/utils`

## Shared API Shape

The shared module should expose one primary entry point for window draws and one for root draws.

Suggested shape:

```c
BOOL BuildWindowVisibleRegion(
    LPWINDOW Window,
    LPRECT BaseScreenRect,
    BOOL ExcludeTargetChildren,
    LPRECT_REGION Region,
    LPRECT Storage,
    UINT Capacity
);

BOOL BuildRootVisibleRegion(
    LPWINDOW RootWindow,
    LPRECT BaseScreenRect,
    LPRECT_REGION Region,
    LPRECT Storage,
    UINT Capacity
);
```

Supporting internal helpers should also be centralized there:

- subtract one occluder rectangle from one region
- subtract one visible window subtree from one region
- enumerate visible siblings in front of one target window

## Canonical Rules

The shared builder must apply the same rules everywhere.

For one non-root target window:

1. Initialize the output region from the caller-provided base screen rectangle.
2. Subtract all visible sibling subtrees whose order is in front of the target window.
3. Optionally subtract all visible child subtrees of the target window.
4. Return the resulting region without collapsing it to one bounding rectangle.

For the desktop root:

1. Initialize the output region from the caller-provided base screen rectangle.
2. Subtract all visible child subtrees of the root window.
3. Return the resulting region without collapsing it to one bounding rectangle.

These are exactly the semantics already expressed by the cursor path.

## Migration Plan

### Step 1

Move the duplicated region primitives out of [Desktop-Graphics.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Graphics.c) and [Desktop-Cursor.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Cursor.c):

- rectangle append helper
- rectangle-minus-occluder helper
- region-minus-occluder helper
- subtree subtraction helper

The implementation must preserve the cursor behavior exactly.

### Step 2

Re-implement `DesktopCursorBuildVisibleRegionForWindow()` as a thin wrapper over the shared module, or remove it entirely if no longer needed.

Target result:

- cursor path becomes a direct consumer of the shared builder
- no cursor-specific visibility logic remains

### Step 3

Re-implement `BuildWindowDrawClipRegion()` on top of the same shared builder.

Important:

- keep `DesktopConsumeWindowDirtyRegionSnapshot()` as the source of dirty input rectangles
- for each dirty rectangle, feed the same visible-region builder used by the cursor path
- merge the produced visible rectangles into the dispatch region

This keeps the dirty-region pipeline intact while unifying the visibility calculation.

### Step 4

Re-implement `BuildDesktopRootVisibleClipRegion()` on top of the same shared builder.

Target result:

- root draw no longer has its own visibility algorithm
- it becomes only a root-specific parameterization of the same engine

### Step 5

Audit any remaining direct callers that subtract siblings or child subtrees manually.

Likely files to audit:

- [Desktop-Graphics.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Graphics.c)
- [Desktop-Cursor.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Cursor.c)
- [Desktop-OverlayInvalidation.c](/home/jango/code/exos/kernel/source/desktop/Desktop-OverlayInvalidation.c)
- [Desktop-Draw.c](/home/jango/code/exos/kernel/source/desktop/Desktop-Draw.c)

Any local visibility recomputation must be replaced by the shared helper.

## Constraints

The refactor must preserve these properties:

- `GRAPHICSCONTEXT` remains region-based
- draw dispatch still uses the existing message and redraw pipeline
- dirty-region consumption remains where it is today
- cursor rendering remains an overlay final pass
- z-order semantics remain unchanged

## Verification

The change is valid only if all these cases use the same visible-region logic and render identically:

1. first desktop draw with `OnScreenDebugInfo` marked bottom-most
2. first draw of the floating test window
3. cursor crossing the overlap zone between both windows
4. dragging the floating window over `OnScreenDebugInfo`
5. root redraw of the exposed area after drag

The expected result is not "less broken". The expected result is:

- no region discrepancy between cursor redraw and window redraw
- no title-bar late appearance
- no cursor-only recovery of hidden pixels
- no drag trail over `OnScreenDebugInfo`

## Implementation Order

Recommended order:

1. introduce the shared region module
2. switch cursor path to the shared builder first
3. switch window draw path second
4. switch root draw path third
5. remove duplicated local helpers
6. build and validate after each switch

This order keeps the known-good cursor behavior as the baseline during the refactor.
