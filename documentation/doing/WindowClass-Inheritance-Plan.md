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
- [ ] Add userland/kernel API to register window classes (`RegisterWindowClass`).
- [ ] Add userland/kernel API to unregister window classes (`UnregisterWindowClass`).
- [ ] Extend window creation API so userland creates windows by class name/handle.

## Step 3 - `BaseWindowFunc` and compatibility migration
- [ ] Add `BaseWindowFunc(...)` as the public API (superclass call).
- [ ] Implement superclass resolution from current dispatch context.
- [ ] Keep temporary compatibility path from `DefWindowFunc` to `BaseWindowFunc`, then remove.

## Step 4 - Window creation and component integration
- [ ] Migrate desktop/windowing components to class inheritance chain.
- [ ] Implement docking components as inherited window classes.

## Step 5 - Validation and documentation
- [ ] Validate behavior on x86-32 and x86-64 (create, draw, input, move/size, delete).
- [ ] Verify superclass chaining with at least two inheritance levels.
- [ ] Update `documentation/Kernel.md` with `WindowClass`, `BaseWindowFunc`, and `EWM_NOT_HANDLED` contract.
