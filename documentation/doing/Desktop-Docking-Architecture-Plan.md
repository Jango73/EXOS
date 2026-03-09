# Desktop Docking Architecture Plan (DockHost + Dockable)

## Scope
- Introduce a generic docking layout engine reusable by desktop root and regular windows.
- Provide deterministic docking on all host edges (`TOP`, `BOTTOM`, `LEFT`, `RIGHT`) with multi-item stacking on each edge.
- Support side-by-side dockables on the same edge with stable ordering rules.
- Keep docking mechanics independent from desktop-specific widgets such as taskbar, launcher, and notification area.
- Deliver one first desktop component (`DesktopShellBar`) implemented on top of the generic docking engine.

## Non-goals
- No hard dependency on a specific visual style or theme recipe.
- No desktop-only API in core docking modules.
- No ad-hoc per-widget geometry logic duplicated across components.
- No animated docking transitions in the first milestone.
- No persisted user preference format in the first milestone.

## Design Rules
- Keep generic docking code outside desktop-specific files.
- Keep all visual look theme-driven (colors, borders, spacing, sizes, typography, assets, effects).
- Implement behavior only in docking modules and components; do not hardcode look in code paths.
- Use neutral names and types:
  - `DOCK_EDGE`
  - `DOCK_AXIS`
  - `DOCKABLE`
  - `DOCK_HOST`
- Allow one host to be bound to any container rectangle:
  - desktop root window area,
  - normal window client area,
  - nested panel area.
- Keep module responsibilities strict:
  - `Dockable` exposes desired size, policies, and callbacks.
  - `DockHost` resolves ordering, computes layout, and assigns rectangles.
- Preserve deterministic results for equal-priority elements with explicit tie-break rules.
- Keep source split by responsibility and under file size limits.
- Keep logs concise and actionable (`WARNING`/`ERROR`) and detailed traces in `DEBUG`.

## Step 0 - Contract freeze and vocabulary
- [x] Define and document neutral docking vocabulary for all UI layers.
- [x] Define exact edge semantics for horizontal and vertical docking.
- [x] Define the canonical coordinate rules for host rect, assigned rect, and remaining work rect.
- [x] Define deterministic ordering fields (`Priority`, `Order`, insertion index fallback).
- [x] Define constraints and rejection behavior for invalid docking requests.
- [x] Freeze shell bar content policy: config-driven component composition, runtime-driven process/window icon entries, deterministic default set when config is missing.

Deliverable:
- A short contract note describing host/dockable terminology and geometry invariants used by all modules.

### Step 0 Output - Docking Contract v1

#### Vocabulary
- `DockHost`: layout container that owns one host rectangle and computes dock placement.
- `Dockable`: behavior object that requests docking on one edge and receives assigned rectangle.
- `WorkRect`: remaining rectangle after all edge reservations are applied.
- `HostRect`: full available rectangle given to one `DockHost`.
- `AssignedRect`: final rectangle applied to one dockable.

#### Edge semantics
- `TOP` and `BOTTOM` reserve vertical bands from host top or bottom.
- `LEFT` and `RIGHT` reserve horizontal bands from current work rectangle left or right.
- Placement order across edges is fixed for determinism:
  - pass 1: `TOP`, `BOTTOM`, `LEFT`, `RIGHT`.
- Within one edge:
  - `TOP`/`BOTTOM` dockables are laid out left-to-right.
  - `LEFT`/`RIGHT` dockables are laid out top-to-bottom.

#### Geometry invariants
- All coordinates are inclusive rectangle coordinates (`X1`, `Y1`, `X2`, `Y2`).
- `HostRect` is immutable during one layout pass.
- `AssignedRect` must always be inside `HostRect`.
- `WorkRect` monotically shrinks or remains unchanged during one layout pass.
- Zero-or-negative requested size is invalid.
- Margin, spacing, padding, and all visual look values are theme-resolved inputs; docking logic only consumes numeric behavior inputs.

#### Ordering contract
- Primary key: `Priority` ascending.
- Secondary key: `Order` ascending.
- Tertiary key: stable insertion index ascending.
- Equal keys produce deterministic output due to insertion index fallback.

#### Rejection and error behavior
- Invalid edge value: reject update and preserve previous valid state.
- Invalid size policy or size value: reject update and preserve previous valid state.
- Attach duplicate dockable handle to same host: reject with explicit error code.
- Overflow policy is explicit and host-configured:
  - clip,
  - shrink respecting minimum constraints,
  - reject placement.
- Rejections return explicit status codes and emit concise diagnostics.

#### Shell bar content policy
- Shell bar content composition is config-driven through `exos.*.toml`.
- Process/window icon entries are runtime-driven from task/window source and are not static config entries.
- Missing shell bar composition config loads a deterministic default component set.

## Step 1 - Generic API and type model
- [x] Add public generic headers:
  - `kernel/include/ui/layout/Dockable.h`
  - `kernel/include/ui/layout/DockHost.h`
- [x] Add central enums and structs:
  - `DOCK_EDGE`
  - `DOCK_LAYOUT_POLICY`
  - `DOCK_SIZE_REQUEST`
  - `DOCK_LAYOUT_RESULT`
- [x] Define callback signatures:
  - `Measure`
  - `ApplyRect`
  - `OnDockChanged`
  - `OnHostWorkRectChanged`
- [x] Define explicit return/error codes for layout acceptance/rejection.
- [x] Keep API ABI-safe and compatible with existing kernel type conventions.
- [x] Keep core API behavior-only; no visual/render style fields in generic docking structures.

Deliverable:
- Header-level docking interface ready for desktop and window modules.

### Step 1 Output - API Header Surface
- Public headers created:
  - `kernel/include/ui/layout/Dockable.h`
  - `kernel/include/ui/layout/DockHost.h`
- Generic type model frozen for implementation phase:
  - neutral enums (`DOCK_EDGE`, `DOCK_AXIS`, `DOCK_LAYOUT_POLICY`),
  - explicit status codes (`DOCK_LAYOUT_STATUS_*`),
  - behavior contracts (`DOCK_SIZE_REQUEST`, `DOCK_LAYOUT_RESULT`).
- Callback contracts frozen:
  - `Measure`,
  - `ApplyRect`,
  - `OnDockChanged`,
  - `OnHostWorkRectChanged`.
- No visual style fields are part of the generic API; theme-driven look remains outside core docking headers.

## Step 2 - Core Dockable implementation
- [x] Implement `Dockable.c` in `kernel/source/ui/layout`.
- [x] Add lifecycle helpers for init/reset without global state.
- [x] Track immutable identity and mutable docking state separately.
- [x] Validate edge/size/order updates before committing state.
- [x] Expose one minimal diagnostic snapshot helper for debugging layout state.

Deliverable:
- Reusable dockable object with validated state transitions.

### Step 2 Output - Dockable Core
- Implemented `kernel/source/ui/layout/Dockable.c` with:
  - deterministic mutable defaults (`Edge`, ordering fields, size request, callbacks),
  - immutable identity model (`Identifier`, `Context`) preserved by reset path,
  - validated updates for edge and size request before commit.
- Added diagnostic snapshot API:
  - `DOCKABLE_SNAPSHOT`
  - `DockableGetSnapshot(...)`
- Build integration updated so `source/ui/layout/*.c` is compiled by kernel build.

## Step 3 - Core DockHost implementation
- [x] Implement `DockHost.c` in `kernel/source/ui/layout`.
- [x] Support attach/detach operations for multiple dockables.
- [x] Maintain per-edge ordered collections with deterministic tie-break.
- [x] Implement full layout pass:
  - consume edge bands,
  - place dockables on each edge,
  - compute final host work rect.
- [x] Support same-edge side-by-side placement policy.
- [x] Reject impossible placements cleanly with diagnostics.

Deliverable:
- Generic host engine producing stable docked rectangles plus remaining work area.

### Step 3 Output - DockHost Core
- Implemented `kernel/source/ui/layout/DockHost.c` with:
  - host lifecycle (`DockHostInit`, `DockHostReset`),
  - attach/detach API with duplicate-pointer and duplicate-identifier rejection,
  - per-edge bucket build + deterministic sort (`Priority`, `Order`, `InsertionIndex`),
  - full relayout pipeline in fixed pass order (`TOP`, `BOTTOM`, `LEFT`, `RIGHT`),
  - side-by-side assignment on each edge band,
  - work-rectangle reduction and result reporting (`AppliedCount`, `RejectedCount`, `Status`).
- Overflow behaviors are enforced through host edge policy (`CLIP`, `SHRINK`, `REJECT`).
- Host notifies dockables of work-rect updates through `OnHostWorkRectChanged` callback.

## Step 4 - Layout policies and edge behavior
- [x] Define edge-specific placement policy:
  - top/bottom: primary axis left-to-right,
  - left/right: primary axis top-to-bottom.
- [x] Define overflow behavior:
  - clip,
  - shrink with minimum size,
  - reject with explicit error.
- [x] Define fill policy support (`Auto`, `Fixed`, `Weighted`).
- [x] Define margins, spacing, and host padding handling.
- [x] Route margins/spacing/padding values through theme tokens in desktop/window bridge layers.
- [x] Keep generic `DockHost` independent from theme parser/runtime (host receives resolved numeric values only).
- [x] Ensure visual interpretation of spacing values is owned by theme contract, not by docking logic.
- [x] Keep policy extension path explicit for future advanced modes.

Deliverable:
- Formal policy behavior document and implementation aligned with API contract.

### Step 4 Output - Policy Engine
- `DockHost` layout now enforces edge policy semantics:
  - top/bottom use horizontal primary axis,
  - left/right use vertical primary axis.
- Overflow policy behavior implemented in layout path:
  - `DOCK_OVERFLOW_POLICY_REJECT`: deterministic rejection with explicit status,
  - `DOCK_OVERFLOW_POLICY_SHRINK`: spacing and item allocation shrink to fit,
  - `DOCK_OVERFLOW_POLICY_CLIP`: item rectangles are clipped by remaining axis bounds.
- Fill policy behavior is applied from dockable requests:
  - `DOCK_LAYOUT_POLICY_FIXED`: fixed requested primary allocation,
  - `DOCK_LAYOUT_POLICY_WEIGHTED`: weighted distribution from remaining space,
  - `DOCK_LAYOUT_POLICY_AUTO`: equal-share distribution fallback.
- Margins, spacing, and host padding are applied numerically via `DOCK_HOST_LAYOUT_POLICY`.
- Generic core remains theme-agnostic by design:
  - no dependency on theme parser/runtime in `DockHost`,
  - theme systems are expected to resolve numeric policy inputs in bridge layers.

## Step 5 - Concurrency and message integration
- [x] Define lock usage rules for docking operations within existing desktop/window mutex contract.
- [x] Ensure no callback execution while holding structural locks that forbid callback/post operations.
- [x] Provide one safe two-phase flow:
  - structural snapshot under lock,
  - callback application outside structural lock.
- [x] Define relayout trigger points:
  - host rect change,
  - dockable property change,
  - attach/detach,
  - visibility change.

Deliverable:
- Deadlock-safe docking integration strategy consistent with desktop lock ordering rules.

### Step 5 Output - Concurrency Surface
- Added explicit two-phase `DockHost` API for lock-safe integration:
  - `DockHostBuildLayoutFrame(...)` computes assignments without invoking dockable callbacks,
  - `DockHostApplyLayoutFrame(...)` applies callbacks from a prepared frame.
- Kept compatibility path:
  - `DockHostRelayout(...)` now composes `BuildLayoutFrame` + `ApplyLayoutFrame`.
- Added relayout dirty tracking and trigger reason plumbing:
  - `DockHostMarkDirty(...)`,
  - `DockHostIsRelayoutRequired(...)`,
  - `DOCK_DIRTY_REASON_*` reason codes.
- Defined trigger mapping in code:
  - host rect change -> `DOCK_DIRTY_REASON_HOST_RECT_CHANGED`,
  - host policy change -> `DOCK_DIRTY_REASON_POLICY_CHANGED`,
  - attach/detach -> `DOCK_DIRTY_REASON_ATTACH_DETACH`,
  - manual dockable/visibility updates -> `DockHostMarkDirty(...)` by bridge layer.

## Step 6 - Desktop bridge layer
- [ ] Add desktop-specific bridge module:
  - `kernel/include/desktop/components/Desktop-DockingBridge.h`
  - `kernel/source/desktop/components/Desktop-DockingBridge.c`
- [ ] Bind one `DockHost` instance to desktop root window.
- [ ] Trigger relayout on desktop mode/size changes.
- [ ] Expose helper API for desktop components to register as dockables.
- [ ] Keep bridge code thin and free of generic layout logic duplication.

Deliverable:
- Desktop can host generic dockables through a dedicated integration bridge.

## Step 7 - Window bridge layer
- [ ] Add window-level bridge support so any window client area can own a `DockHost`.
- [ ] Ensure nested hosts are supported (window containing docked sub-panels).
- [ ] Keep ownership/lifetime tied to window lifecycle (`EWM_CREATE`/`EWM_DELETE`).
- [ ] Recompute host rect on move/size events and apply relayout.
- [ ] Add helper methods for window components to register/unregister dockables.

Deliverable:
- Generic docking is usable inside normal windows, including multiple dockables on one edge.

## Step 8 - DesktopShellBar component
- [ ] Add component files:
  - `kernel/include/desktop/components/Desktop-ShellBar.h`
  - `kernel/source/desktop/components/Desktop-ShellBar.c`
- [ ] Implement `DesktopShellBar` as a dockable consumer of generic APIs.
- [ ] Support all four edges through dock state updates.
- [ ] Keep `DesktopShellBar` behavior-focused; all visual look is resolved from theme tokens.
- [ ] Keep visual/render logic isolated from docking logic and driven by theme runtime values only.
- [ ] Reserve extension points for task list, launcher, tray, and clock zones.
- [ ] Implement shell bar content slots as pluggable components.
- [ ] Keep process/window icons managed by the dynamic task list source, not by static config entries.

Deliverable:
- First production component using generic docking, with edge docking on desktop.

## Step 9 - Validation and regression tests
- [ ] Add deterministic test scenarios for:
  - single dockable per edge,
  - multiple dockables same edge side-by-side,
  - mixed edges with work-area shrink,
  - attach/detach churn,
  - relayout after host resize.
- [ ] Validate behavior on x86-32 and x86-64 smoke paths.
- [ ] Verify no fault in kernel logs during repeated relayout stress.
- [ ] Add targeted stress path for rapid dock property changes.
- [ ] Validate shell bar composition loading:
  - config present applies configured components,
  - config missing loads default component set,
  - process/window icon entries always come from runtime task list.

Deliverable:
- Docking behavior verified under deterministic and stress scenarios.

## Step 10 - Diagnostics and tooling
- [ ] Add concise diagnostics for host/dockable state snapshots:
  - host rect,
  - per-edge dock list order,
  - assigned rect per dockable,
  - remaining work rect.
- [ ] Add rate-limited warnings for invalid or rejected docking requests.
- [ ] Add one debug command path to print active docking topology.
- [ ] Keep protocol-level verbose traces in `DEBUG` only.

Deliverable:
- Fast troubleshooting path for layout issues without log flooding.

## Step 11 - Configuration and persistence extension path
- [ ] Define stable config schema for persisted docking preferences:
  - edge,
  - order,
  - size policy.
- [ ] Define shell bar content composition schema in `exos.*.toml` (except process/window icon entries, which are runtime-driven).
- [ ] If shell bar content config is absent, load one deterministic default component set.
- [ ] Bind config keys through `KernelPath` logical path utilities.
- [ ] Add migration-safe parser strategy for schema evolution.
- [ ] Keep fallback behavior deterministic for invalid persisted state.

Deliverable:
- Architecture-ready persistence path without coupling core engine to storage details.

## Step 12 - Documentation and maintenance
- [ ] Update `documentation/Kernel.md` with the docking subsystem architecture.
- [ ] Add doxygen headers for all public docking APIs.
- [ ] Document integration examples for:
  - desktop root host,
  - regular window host,
  - nested host composition.
- [ ] Document extension checklist for future dockable components.
- [ ] Add one explicit rule in kernel documentation: docking code implements behavior only, theme controls full look.

Deliverable:
- Docking subsystem documented for long-term maintenance and extension.
