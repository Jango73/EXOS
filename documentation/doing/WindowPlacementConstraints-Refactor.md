# Window Placement Constraints Refactor

## Goals

- Remove all direct knowledge between `kernel/source/desktop/` and `kernel/source/ui/`.
- Restrict `ui/` to high-level desktop/window APIs only.
- Move placement exclusion rules out of docking/component code and into a generic windowing constraint mechanism.
- Ensure placement constraints apply to all window placement paths, not only pointer-driven moves.

## Rules

- `desktop/` must ignore everything in `ui/`.
- `ui/` must ignore all desktop internals and use only public high-level APIs.
- No component-specific property names, classes, or layout behavior may be read by core windowing code.
- Placement exclusion must be expressed as generic window constraints, not as docking-specific special cases.

## Steps

- [x] Define a generic window placement-constraint API in high-level windowing headers.
  - Support at least one exclusion mode equivalent to: "this window reserves its rectangle against sibling placement".
  - Keep the API behavior-oriented so future constraints can be added without coupling to docking.

- [x] Refactor core window placement so every path uses the same constraint resolver.
  - Cover create, move, size, programmatic rect changes, and drag-driven changes.
  - Make the resolver operate only on generic window state and generic constraints.

- [x] Refactor docking/components to publish constraints only through the new API.
  - Remove any remaining `desktop <-> components` dependency.
  - Make dock hosts/dockables consumers of the generic constraint system rather than owners of placement policy in core windowing.

- [ ] Update `OnScreenDebugInfo` and related test windows to use only high-level APIs and verify constrained placement behavior against reserved sibling areas.

## Validation

- [ ] Confirm that no file in `desktop/` includes or references symbols from `ui/`.
- [ ] Confirm that components use only public high-level APIs.
- [ ] Confirm that a window created directly inside a reserved sibling area is rejected or clamped by the generic constraint path.
- [ ] Update `documentation/Kernel.md` with the final architecture and constraint contract.
