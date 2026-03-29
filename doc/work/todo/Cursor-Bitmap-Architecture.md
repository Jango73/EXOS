# Cursor and Bitmap Architecture Plan

## Purpose
Define one kernel-owned bitmap model that can serve multiple subsystems, with cursor management built on top of it.

This plan introduces:
- one reusable `BITMAP` kernel object,
- one cursor policy layer that consumes `BITMAP`,
- one userland ABI to select cursor roles and window-level overrides.

## Goals
- Make `BITMAP` a first-class reusable kernel object.
- Allow a cursor to use a `BITMAP` directly.
- Allow derived cursor bitmaps (copy, sub-bitmap, tint, alpha policy, hotspot change).
- Support a cursor role set (`default`, `drag`, `drag_add`, `info`, `help`, and extension roles).
- Support per-window override for one role or many roles.
- Keep hardware and software cursor rendering paths unified around one active cursor selection.
- Keep ABI backward-compatible for existing userland windowing applications.

## Non-Goals
- No full compositor migration to userland.
- No driver-specific cursor policy in user applications.

## Core Principles
- Mechanics in kernel, policy at desktop/window boundary, representation in reusable objects.
- One source of truth for active cursor selection.
- Theme provides default cursor-role mappings; API provides runtime overrides.
- Cursor visual assets are data (`BITMAP`), not hardcoded draw templates.
- Derived bitmap operations are generic utilities, not cursor-specific local code.
- Unimplemented optional driver commands return `DF_RETURN_NOT_IMPLEMENTED`.

## Theme and API Responsibilities
- Theme defines default cursor set mapping (`Role -> Cursor`) for visual consistency.
- API defines runtime intent and per-window/per-context overrides.
- Cursor resolver applies deterministic precedence:
1. API explicit window override.
2. System transient policy role.
3. Theme default role mapping.
4. Built-in fallback cursor.

## Object Model

### `BITMAP` kernel object
Add `KOID_BITMAP` and one `BITMAP` structure managed through `CreateKernelObject` and `ReleaseKernelObject`.

Suggested fields:
- `Width`, `Height`
- `Format` (start with `ARGB8888`)
- `Pitch`
- `Flags` (immutable, user-owned, kernel-owned, shared)
- `OwnerProcess`
- `PixelStorage` pointer
- `StorageBytes`

`BITMAP` is reusable by cursor, icons, and future non-client rendering assets.

### `CURSOR` kernel object
Add `KOID_CURSOR` as a policy wrapper referencing one `BITMAP`.

Suggested fields:
- `Bitmap` (handle/pointer to `BITMAP`)
- `HotspotX`, `HotspotY`
- `Flags` (animated reserved, constrained visibility policy)

The cursor object does not duplicate pixel buffers.

## Image Derivation Model
Introduce reusable bitmap derivation operations in a shared module:
- `BitmapCreate`
- `BitmapClone`
- `BitmapCreateSubBitmap`
- `BitmapCreateTinted`
- `BitmapCreateAlphaMapped`
- `CursorCreateWithBitmap` (cursor helper that references same bitmap or cloned bitmap)

Rules:
- Derived bitmaps return new `BITMAP` objects.
- If derivation is metadata-only (for example hotspot change), allow sharing the same source bitmap.
- If derivation changes pixels, allocate a new storage buffer.

Implementation location:
- `kernel/include/utils/Bitmap.h`
- `kernel/source/utils/Bitmap.c`

This keeps generic bitmap mechanics outside desktop-specific code.

## Cursor Roles and Sets
Define a stable cursor role enum in user/kernel ABI:
- `CURSOR_ROLE_DEFAULT`
- `CURSOR_ROLE_DRAG`
- `CURSOR_ROLE_DRAG_ADD`
- `CURSOR_ROLE_INFO`
- `CURSOR_ROLE_HELP`
- extension-safe range for future roles

Define a desktop cursor set:
- `Role -> CursorHandle`

Define a window override set:
- `Role -> CursorHandle` (sparse; missing roles fall back to desktop set)

## Cursor Resolution Pipeline
Create one resolver module:
- `DesktopCursorResolveActive`

Inputs:
- desktop capture state,
- hover target window,
- non-client hit-test result,
- window requested role,
- window override set,
- desktop default set,
- fallback cursor.

Priority order:
1. System transient policy (for example non-client drag/resize policy role).
2. Captured window requested role with its overrides.
3. Hover window requested role with its overrides.
4. Desktop role mapping.
5. Built-in fallback cursor.

Output:
- resolved `CURSOR` object,
- resolved `BITMAP`,
- hotspot and visibility metadata.

## Rendering Integration

### Hardware cursor path
When cursor selection changes:
- upload resolved bitmap through `DF_GFX_CURSOR_SET_SHAPE`,
- apply hotspot,
- keep position and visibility path unchanged.

When only position changes:
- keep `DF_GFX_CURSOR_SET_POSITION` only.

### Software overlay path
Replace hardcoded arrow template rendering with resolved `BITMAP` blit.

Requirements:
- clipping remains bounded to visible region,
- alpha blending honors `ARGB8888`,
- invalidation region includes old and new cursor rectangles.

## Window and Desktop Integration

### Window state additions
For each `WINDOW`:
- requested cursor role,
- sparse role override map,
- optional explicit cursor handle (shortcut for `CURSOR_ROLE_DEFAULT`).

### Desktop state additions
For each `DESKTOP`:
- default cursor set,
- active resolved cursor handle,
- active resolved bitmap handle,
- cache key to avoid redundant backend shape updates.

## Userland ABI
Add explicit syscall payloads in `User.h`.

### Bitmap services
- `SYSCALL_CreateBitmap`
- `SYSCALL_CreateDerivedBitmap`
- `SYSCALL_GetBitmapInfo`

### Cursor services
- `SYSCALL_CreateCursor`
- `SYSCALL_SetDesktopCursorRole`
- `SYSCALL_SetWindowCursorRole`
- `SYSCALL_SetWindowCursorOverride`
- `SYSCALL_ClearWindowCursorOverride`
- `SYSCALL_GetCursorInfo`

Runtime wrappers in:
- `runtime/include/exos.h`
- `runtime/source/exos.c`

## Compatibility Rules
- If no bitmap/cursor is configured, use built-in fallback arrow bitmap.
- Existing mouse message flow remains unchanged.
- Existing applications compile and run without API migration.

## Security and Ownership
- `BITMAP` and `CURSOR` handles are validated through existing handle mapping and object type checks.
- Window override calls validate that caller owns target window process or has required privilege.
- Reject malformed pixel buffers and overflow-prone dimensions.

## Logging and Diagnostics
- `WARNING` and `ERROR` stay concise and actionable.
- Detailed dump (role resolution chain, selected handle identifiers, backend shape upload status) stays in `DEBUG`.
- Add rate-limited diagnostics for repeated shape upload failures.

## Implementation Steps

## Step 1 - Add generic `BITMAP` object
- [ ] Add `KOID_BITMAP` and base struct in kernel object model.
- [ ] Add lifecycle helpers and validation.
- [ ] Add `Bitmap` utility module in `kernel/include/utils` and `kernel/source/utils`.

Deliverable:
- Kernel can create/delete/query reusable bitmaps.

## Step 2 - Add `CURSOR` object using `BITMAP`
- [ ] Add `KOID_CURSOR` and cursor struct.
- [ ] Add cursor creation from direct bitmap reference.
- [ ] Add hotspot validation.

Deliverable:
- Cursor objects exist independently of desktop rendering path.

## Step 3 - Add derivation API on `BITMAP`
- [ ] Implement clone/sub-image/tint/alpha operations.
- [ ] Keep derivation reusable and cursor-agnostic.

Deliverable:
- Any subsystem can create derived bitmap assets.

## Step 4 - Add desktop/window role mapping and overrides
- [ ] Add role enum in ABI.
- [ ] Add desktop role set storage.
- [ ] Add per-window sparse override storage and requested role field.

Deliverable:
- Window-level override works for one role or multiple roles.

## Step 5 - Integrate cursor defaults in theme runtime
- [ ] Extend theme schema with cursor role mapping keys (`window.cursor.default`, `window.cursor.drag`, `window.cursor.help`, etc.).
- [ ] Add parser/resolver support in desktop theme modules.
- [ ] Load theme cursor role defaults into desktop cursor set at theme activation.
- [ ] Keep deterministic fallback when one role is missing in the theme.

Deliverable:
- Theme activation provides a complete default cursor-role set consumed by resolver.

## Step 6 - Add cursor resolver module
- [ ] Implement deterministic role resolution order.
- [ ] Integrate with hit-test and capture states.
- [ ] Return one resolved cursor/bitmap pair.

Deliverable:
- One central resolver controls cursor selection.

## Step 7 - Integrate with rendering paths
- [ ] Replace hardcoded software arrow template path with bitmap blit.
- [ ] Add shape-cache gate for hardware upload.
- [ ] Keep position-only fast path.

Deliverable:
- Hardware and software cursor paths consume the same resolved cursor data.

## Step 8 - Add userland syscall/runtime API
- [ ] Add syscall identifiers and payload structs.
- [ ] Add syscall handlers with object validation.
- [ ] Add runtime wrappers.
- [ ] Implement missing runtime capture/release mouse wrappers to call syscalls.

Deliverable:
- Userland can create bitmaps/cursors, assign roles, and override per window.

## Step 9 - Documentation and tests
- [ ] Update `doc/guides/kernel.md` with `BITMAP`, `CURSOR`, resolver flow, and ABI.
- [ ] Add kernel shell diagnostics command for cursor role resolution state.
- [ ] Add smoke tests for role switching, per-window overrides, fallback behavior, and drag policy role.

Deliverable:
- Architecture and behavior are documented and testable.

## Data Structures (Draft)

```c
// ABI-visible role identifier
#define CURSOR_ROLE_DEFAULT   0x00000000
#define CURSOR_ROLE_DRAG      0x00000001
#define CURSOR_ROLE_DRAG_ADD  0x00000002
#define CURSOR_ROLE_INFO      0x00000003
#define CURSOR_ROLE_HELP      0x00000004
```

```c
typedef struct tag_BITMAP {
    LISTNODE_FIELDS
    MUTEX Mutex;
    U32 Width;
    U32 Height;
    U32 Format;
    U32 Pitch;
    U32 Flags;
    U32 StorageBytes;
    LPVOID PixelStorage;
} BITMAP, *LPBITMAP;
```

```c
typedef struct tag_CURSOR {
    LISTNODE_FIELDS
    MUTEX Mutex;
    LPBITMAP Bitmap;
    U32 HotspotX;
    U32 HotspotY;
    U32 Flags;
} CURSOR, *LPCURSOR;
```

## Open Design Decisions
- Maximum cursor/bitmap dimensions for ABI exposure.
- Whether bitmap formats beyond `ARGB8888` are accepted in first delivery.
- Whether animated cursor sequencing is deferred or included as a separate object.

## Acceptance Criteria
- A userland window can set `CURSOR_ROLE_HELP` and get the expected cursor while hovered.
- A userland window can override `CURSOR_ROLE_DRAG_ADD` without changing other roles.
- Dragging non-client area switches to drag policy cursor and restores afterward.
- Hardware and software cursor paths display the same visual for a given resolved role.
- No regression in input routing and message delivery (`EWM_MOUSEMOVE`, `EWM_MOUSEDOWN`, `EWM_MOUSEUP`).
