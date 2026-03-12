# Windowing Architecture Roadmap

## Purpose
Define a kernel-driven windowing architecture that stays simple, evolves safely, and supports advanced visual customization through theme files.

## Goals
- Keep window lifecycle, input routing, composition, clipping, invalidation, and Z-order in the kernel.
- Separate mechanics from look.
- Support theme changes without changing application behavior.
- Keep ABI stable for existing userland code.
- Allow advanced decorations without implementing a full CSS engine.
- Allow fully client-rendered windows for frameworks that draw all UI themselves.

## Non-Goals
- Reimplement Win32, X11, or Wayland.
- Parse full CSS in the kernel.
- Move compositor ownership to userland.

## Core Principles
- Kernel owns mechanics.
- Theme data owns look.
- Applications own client-area behavior.
- Non-client rendering is centralized in kernel policy.
- Theme input is strict data, never executable logic.

## High-Level Architecture
1. Window Server (Kernel)
- Window creation/destruction.
- Parent/child hierarchy.
- Focus, capture, activation, ordering.
- Input routing.

2. Compositor (Kernel)
- Damage tracking.
- Clip resolution.
- Draw scheduling.
- Driver submission.

3. Non-Client Renderer (Kernel)
- Frame/title/buttons/borders.
- Non-client hit-test.
- State-based rendering through resolved theme values.
- Enabled only for system-decorated windows.

4. Theme Engine (Kernel)
- Resolves style values for `(element, state)`.
- Resolves decoration recipes for `(element, state)`.
- Exposes typed values with fallback to default theme.

5. Theme Loader
- Kernel loads TOML theme files directly from logical paths.
- No mandatory external theme compiler step.

## Diskless Bootstrap Requirement
Windowing must be testable before storage access is available.

Boot-time behavior:
- A built-in default theme is always available in kernel memory.
- Desktop/windowing can start with no filesystem/theme file access.
- TOML themes are optional runtime overrides, not boot prerequisites.

Operational model:
1. Early phase (diskless):
- Use built-in default theme only.
- Allow shell desktop activation from the EXOS console shell (`desktop show`).

2. Late phase (storage ready):
- Load TOML theme if requested.
- If load/validate/activate fails, keep current built-in/default active theme.

## Window Decoration Modes
Each window declares one decoration mode at creation time.

1. `SystemDecorated`
- Kernel renders non-client area (frame/title/system buttons) using theme engine.
- Application renders client area.

2. `ClientDecorated`
- Kernel renders no visual non-client area.
- Application/framework renders the full window (chrome + content).
- Kernel still handles lifecycle, input routing, clipping, composition, focus, and presentation.

3. `BareSurface`
- Strict variant for raw surfaces.
- No non-client and minimal policy assumptions.
- Useful for game engines or custom composited shells.

## Why Not Full CSS
- Too complex for kernel parsing and validation.
- High implementation and maintenance cost.
- Unneeded features for OS chrome and controls.

Target approach:
- CSS-inspired naming and selectors.
- Strict and small TOML schema.
- Deterministic lookup and bounded parsing.

## Theme Source Format (TOML)
Theme files are text TOML, parsed by the existing parser.

Example paths:
- `/system/themes/default/theme.toml`
- `/system/themes/classic/theme.toml`
- `/system/themes/flat-dark/theme.toml`

Top-level sections:
- `[theme]`: metadata.
- `[tokens]`: semantic values reused everywhere.
- `[elements.<element-id>]`: simple style properties.
- `[recipes.<recipe-id>]`: advanced draw primitive lists.
- `[bindings]`: maps elements/states to recipe identifiers.

## Decorable Elements Naming
Use canonical element identifiers. These names are part of the theme contract.

Core desktop and window elements:
- `desktop.root`
- `window.client`
- `window.titlebar`
- `window.title.text`
- `window.border`
- `window.button.close`
- `window.button.maximize`
- `window.button.minimize`
- `window.resize.left`
- `window.resize.right`
- `window.resize.top`
- `window.resize.bottom`
- `window.resize.top_left`
- `window.resize.top_right`
- `window.resize.bottom_left`
- `window.resize.bottom_right`

Control elements:
- `button.body`
- `button.text`
- `textbox.body`
- `textbox.text`
- `textbox.caret`
- `menu.background`
- `menu.item`
- `menu.item.text`

## State Naming
State identifiers:
- `normal`
- `hover`
- `pressed`
- `focused`
- `active`
- `disabled`
- `checked`
- `selected`

State resolution:
- Exact match first.
- Then partial fallback chain.
- Then `normal`.

## Two-Level Styling Model
The theme engine resolves styling in two layers.

1. Level 1: Semantic Tokens and Basic Properties
- Fast path for most themes.
- Single-value properties mapped to colors/metrics/flags.
- Suitable for classic single-fill styling.

2. Level 2: Decoration Recipes
- For advanced visuals.
- Element/state can reference a recipe made of draw primitives.
- Enables gradients, layered borders, 3D effects, and glyph overlays without custom code per theme.

## Level 1: Tokens and Basic Properties
Example:

```toml
[tokens]
color.desktop.background = "#002b36"
color.window.border = "#000000"
color.client.background = "#202020"
color.window.title.active.start = "#1f4a9a"
color.window.title.active.end = "#4f89ff"
color.window.title.inactive.start = "#5a5a5a"
color.window.title.inactive.end = "#7a7a7a"
metric.window.border = 2
metric.window.title_height = 22
metric.window.button_size = 18
metric.window.button_spacing = 4
```

Element style with state overrides:

```toml
[elements.window.titlebar]
background = "token:color.window.title.active.start"
background2 = "token:color.window.title.active.end"
title_height = "token:metric.window.title_height"

[elements.window.titlebar.states.inactive]
background = "token:color.window.title.inactive.start"
background2 = "token:color.window.title.inactive.end"
```

## Level 2: Recipe Primitives
Recipes describe ordered primitives drawn by the kernel renderer.

Supported primitive kinds (initial set):
- `fill_rect`
- `stroke_rect`
- `line`
- `gradient_v`
- `gradient_h`
- `glyph`
- `inset_rect`

Common primitive fields:
- `x1`, `y1`, `x2`, `y2` as anchors or expressions (`0`, `w-1`, `h-1`).
- `color`, `color1`, `color2`.
- `thickness`.
- `clip_part` (optional).
- `when_state` (optional state filter).

Example recipe:

```toml
[recipes.window_frame_classic]
steps = [
  { op = "stroke_rect", x1 = 0, y1 = 0, x2 = "w-1", y2 = "h-1", color = "#000000", thickness = 1 },
  { op = "stroke_rect", x1 = 1, y1 = 1, x2 = "w-2", y2 = "h-2", color = "#ffffff", thickness = 1 }
]
```

State-aware binding:

```toml
[bindings]
"window.border.normal" = "window_frame_classic"
"window.border.focused" = "window_frame_classic"
"window.button.close.hover" = "window_button_close_hover"
```

This keeps the parser simple while allowing richer decoration than single-color fill.

## Kernel Theme Runtime Responsibilities
- Parse TOML theme file.
- Validate schema, property names, types, limits, and references.
- Resolve token references.
- Build compact in-memory runtime tables.
- Serve lookups for style values and recipes during drawing.
- Fallback to built-in defaults on any validation failure.

## API Direction
Compatibility first, then extension.

1. Keep existing APIs:
- `GetSystemBrush(SM_COLOR_*)`
- `GetSystemPen(SM_COLOR_*)`

2. Internally map `SM_COLOR_*` to theme tokens/properties.

3. Add optional style query API later:
- `GetWindowStyleValue(Window, Part, PropertyId, StateHint, OutValue)`

4. Theme loading/activation API:
- `LoadTheme(Path)`
- `ActivateTheme(NameOrHandle)`
- `GetActiveThemeInfo`
- `ResetThemeToDefault`

5. Window creation and capabilities:
- Add decoration mode flag in window creation style/attributes.
- Proposed style flags:
- `EWS_SYSTEM_DECORATED` (default for compatibility)
- `EWS_CLIENT_DECORATED`
- Optional: `EWS_BARE_SURFACE`
- Ensure hit-test/input path supports client-decorated windows without kernel non-client assumptions.

6. Surface presentation path for UI frameworks:
- Keep or add explicit buffer/surface acquire API.
- Present with dirty rectangles.
- Deliver resize/focus/input events without forcing system chrome rendering.

7. EXOS shell control path (console shell integration):
- Add command to start desktop/windowing from console shell (for diskless testing).
- Add command to load/activate a theme when storage is available.
- Add command to report current desktop/theme state and fallback status.

## Message and Rendering Flow
1. Input event arrives.
2. Kernel hit-test resolves client or non-client target.
3. Kernel dispatches window messages.
4. Invalid regions are merged.
5. Compositor schedules draw.
6. If `SystemDecorated`: non-client renderer draws with Level 1 or Level 2 data.
7. If `ClientDecorated` or `BareSurface`: non-client drawing step is skipped.
8. App window procedure draws client area.
9. Final image is submitted to graphics driver.

## Implementation Steps
### Step 1: Extract Non-Client Rendering
- [x] Create a dedicated non-client renderer module in kernel windowing code.
- [x] Move frame/title/button/border drawing out of the default window function.
- [x] Keep the same visuals as today to avoid regressions.
- [ ] Verify non-client hit-test behavior is unchanged.
- [x] Make non-client rendering path conditional by window decoration mode.

### Step 2: Introduce Default Theme Tokens
- [x] Define built-in default tokens for existing system colors and metrics.
- [x] Replace hardcoded brush/pen constants with token lookups.
- [x] Map existing `SM_COLOR_*` values to token-backed properties.
- [ ] Verify legacy applications still render correctly with `GetSystemBrush`/`GetSystemPen`.
- [x] Ensure token resolution is bypassed for `ClientDecorated` windows where non-client is disabled.

### Step 3: Define TOML Theme Schema
- [x] Freeze top-level sections: `theme`, `tokens`, `elements`, `recipes`, `bindings`.
- [x] Freeze canonical element identifiers (`desktop.root`, `window.border`, `window.titlebar`, etc.).
- [x] Freeze state identifiers and state fallback order.
- [x] Define allowed property names and value types per element family.
- [x] Define parser limits (file size, token count, recipe count, primitive count).

### Step 4: Implement Kernel Theme Parser
- [x] Parse TOML in strict mode.
- [x] Validate all references (token references, recipe bindings, property types).
- [x] Build in-memory runtime tables for fast lookup.
- [x] Implement atomic activation and fallback to built-in default on failure.

### Step 5: Implement Level 1 Resolver (Properties)
- [x] Implement `(element, state) -> property value` resolver.
- [x] Implement state fallback (`exact -> partial -> normal`).
- [x] Integrate resolver into non-client renderer.
- [x] Ensure desktop root and standard window decorations use Level 1 values.

### Step 6: Implement Level 2 Resolver (Recipes)
- [x] Implement recipe lookup from `bindings`.
- [x] Implement primitive interpreter for initial primitive set.
- [x] Support token references inside primitive fields.
- [x] Apply recipe rendering to selected non-client elements.
- [x] Keep deterministic execution and bounded primitive count.

### Step 7: Theme Runtime API
- [x] Implement theme runtime API for load/activate/info/reset.
- [x] Ensure activation invalidates desktop/windows for full redraw.
- [x] Keep compatibility behavior for existing applications.
- [x] Add diagnostics for invalid themes and fallback reason.
- [x] Ensure theme activation does not affect `ClientDecorated` non-client rendering (none is rendered).

### Step 8: Diskless Desktop Bootstrap From EXOS Shell
- [x] Add shell command `desktop show` to create or reuse the shell desktop and launch desktop/windowing using built-in theme.
- [x] Add shell command `desktop status` to report active desktop mode and active theme source (`built-in` or `loaded`).
- [x] Add shell command `desktop theme <path-or-name>` to load/activate a theme.
- [x] Add `Desktop.ThemePath` config support in `exos.*.toml` to select the theme file at desktop startup.
- [x] Ensure `desktop show` succeeds when no storage is mounted.
- [x] Ensure theme load failure never prevents desktop startup.

### Step 9: Kernel Internal Desktop Test Module
- [x] Add one internal kernel windowing test module (portal-like purpose, kernel-side implementation).
- [x] Make the test module create two test windows with different sizes.
- [x] Set arbitrary titles on both test windows for visual identification.
- [x] Ensure both test windows are visible after `desktop show`.

### Step 10: Mouse Cursor Ownership and Rendering
- [x] Define cursor ownership: cursor state, position, visibility, and clipping policy are managed by kernel desktop/compositor code.
- [x] Define backend contract: graphics drivers expose optional hardware-cursor capabilities; compositor uses them when available.
- [x] Implement deterministic fallback: when hardware cursor is unavailable, compositor draws a software cursor overlay as the final pass.
- [x] Ensure cursor rendering remains independent from userland window content and decoration modes (`SystemDecorated`, `ClientDecorated`, `BareSurface`).
- [x] Ensure `desktop show` always displays a visible pointer in graphics mode (hardware or software fallback).
- [x] Add diagnostics to report active cursor path (`hardware` or `software`) and fallback reason.

### Step 11: Client-Decorated Framework Support
- [ ] Add decoration mode flag to window creation ABI/runtime API.
- [ ] Implement no-chrome kernel path for `ClientDecorated` windows.
- [ ] Verify input/focus/capture/resize semantics for client-rendered chrome.
- [ ] Add dirty-rectangle present path validation for framework rendering.
- [ ] Validate minimal requirements for Qt-like platform plugins.

### Step 12: Portal/Userland Alignment
- [ ] Update `portal` to stop drawing standard non-client frame visuals.
- [ ] Keep `portal` focused on client-area content and behavior.
- [ ] Validate button and control rendering against canonical element definitions.
- [ ] Verify behavior in both x86-32 and x86-64 configurations.

### Step 13: Testing and Hardening
- [ ] Add functional tests for create/show/hide/destroy/focus/capture.
- [ ] Add state-transition rendering tests (`normal`, `hover`, `pressed`, `focused`, `disabled`).
- [ ] Add compatibility tests for legacy `SM_COLOR_*` consumers.
- [ ] Add compatibility tests for `ClientDecorated` windows (no kernel non-client draw).
- [ ] Add diskless boot tests where `desktop show` activates the shell desktop with built-in theme only.
- [ ] Add post-mount theme switch tests (`built-in -> loaded -> fallback on error`).
- [ ] Add malformed TOML and reference-error tests.
- [ ] Add boundary tests for parser/runtime limits.

### Step 14: Documentation Finalization
- [ ] Update `documentation/Kernel.md` with final architecture.
- [ ] Document TOML schema contract and canonical element/state registry.
- [ ] Document theme API and fallback semantics.
- [ ] Document diskless bootstrap and EXOS shell commands.
- [ ] Document window decoration modes and framework integration expectations.
- [ ] Add a minimal reference default theme file example.

## Portal and Desktop Integration Notes
- `portal` should not hardcode standard frame visuals.
- `portal` should consume system theme values for shared controls.
- Desktop root drawing should use `desktop.root` element style.

## Safety and Stability Rules
- Strict parser mode only.
- Bounded file size, rule count, token count, and primitive count.
- No loops, no scripting, no expression language beyond simple anchored coordinates.
- Unknown property or invalid type produces controlled fallback.
- Theme activation is atomic.

## Testing Strategy
1. Functional
- Window create/show/hide/destroy.
- Focus/capture changes.
- Non-client hit-tests.

2. Visual
- Baseline comparisons for default theme.
- State transition coverage (`normal`, `hover`, `pressed`, `focused`, `disabled`).

3. Compatibility
- Legacy `SM_COLOR_*` users keep expected behavior.
- Existing default window procedure behavior stays stable.

4. Robustness
- Invalid TOML cases.
- Missing tokens and recipe references.
- Limit boundary tests.

## Open Decisions
- Exact token naming convention freeze.
- Initial primitive set freeze.
- Font ownership and text metrics policy.
- Global active theme or per-desktop active theme.

## Global Completion Checklist
- [ ] All implementation steps completed.
- [ ] Default theme renders equivalently to pre-theme behavior.
- [ ] At least one alternate theme is loadable at runtime.
- [ ] No kernel fault on invalid theme input.
- [ ] Architecture and API fully documented.
