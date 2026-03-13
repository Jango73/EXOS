# Windowing Removable Functions Audit

## Scope

Static audit of windowing-related code under:

- `kernel/source/desktop`
- `kernel/source/ui`
- corresponding public headers in `kernel/include/desktop` and `kernel/include/ui`

All functions that were truly never called have been removed from the codebase.

## Explicit non-candidates

These functions have low textual reference counts, but they are still live and should not be grouped with dead APIs.

### `Cube3DGetPreferredSize`

- Live use: `kernel/source/ui/Startup-Desktop-Components.c`

### `OnScreenDebugInfoGetPreferredSize`

- Live use: `kernel/source/ui/Startup-Desktop-Components.c`

### `ShellBarGetWindow`

- Live use: `kernel/source/ui/Startup-Desktop-Components.c`

### `ShellBarEnsureClockWidget`

- Live use: `kernel/source/ui/ShellBar.c`

## Summary

Remaining candidates found: 0

The audit no longer contains any statically dead function in the current tree.
