# Window Class Inheritance Plan (BaseWindowFunc)

## Scope
- Replace external `DefWindowFunc` usage with `BaseWindowFunc`.
- Introduce window-class inheritance for component architecture.
- Keep docking/components built on class inheritance, not per-component syscalls.

## Step 1 - Core class model and dispatch contract
- [x] Add `WINDOW_CLASS` model (`Name`, `BaseClass`, `WindowFunc`, optional class data size).
- [x] Add one kernel class registry and class lookup by name/handle.
- [x] Define dispatch contract with explicit `EWM_NOT_HANDLED` to continue to base class.

## Step 2 - Userland class API
- [x] Add userland/kernel API to register window classes (`RegisterWindowClass`).
- [x] Add userland/kernel API to unregister window classes (`UnregisterWindowClass`).
- [x] Extend window creation API so userland creates windows by class name/handle.

## Step 3 - `BaseWindowFunc` dispatch chain
- [x] Add `BaseWindowFunc(...)` as the public API (superclass call).
- [x] Implement superclass resolution from current dispatch context.
- [x] Remove external `DefWindowFunc` compatibility path.

## Step 4 - Window creation and component integration
- [x] Migrate desktop/windowing components to class inheritance chain.
- [x] Implement docking components as inherited window classes.

## Step 5 - Validation and documentation
- [ ] Validate behavior on x86-32 and x86-64 (create, draw, input, move/size, delete).
- [ ] Verify superclass chaining with at least two inheritance levels.
- [ ] Update `documentation/Kernel.md` with `WindowClass`, `BaseWindowFunc`, and `EWM_NOT_HANDLED` contract.
