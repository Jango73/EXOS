# Graphics Shell Scripting Plan

## Goal

Replace the `gfx` shell command with E0-exposed functions and symbols.

## Shell Command Removal

- Remove the `gfx` shell command from the shell command registry.
- Remove the direct shell-side driver lookup and mode selection logic tied to `gfx`.
- Keep the implementation centered on exposed symbols plus E0 host functions.

## Exposed Root

Add one root symbol:

- `graphics`

## Exposed Symbols

Expose the active frontend:

- `graphics.frontend`

`graphics.frontend` returns one of:

- `console`
- `desktop`

Expose the active driver index:

- `graphics.current_driver_index`
- `graphics.current_driver_alias`

Expose the active mode:

- `graphics.mode.width`
- `graphics.mode.height`
- `graphics.mode.bpp`

Expose the supported modes of each driver through the existing global driver exposure:

- `driver[index].mode[mode_index].width`
- `driver[index].mode[mode_index].height`
- `driver[index].mode[mode_index].bpp`

## Active Driver Contract

The active graphics driver is resolved through:

- `graphics.current_driver_index`
- `graphics.current_driver_alias`

Driver discovery stays on the existing global `driver[index]` exposure.

## E0 Host Function

Add one shell E0 host function:

- `set_graphics_driver(driver_alias, width, height, bpp)`

The first parameter is the target graphics driver alias.
The remaining parameters define the requested mode.

## Questions To Confirm Before Implementation

No remaining open question in this draft.

## Intended Outcome

After this refactor, scripts should be able to:

- inspect the active frontend
- inspect the active mode
- inspect the active graphics driver through `graphics.current_driver_index` and `graphics.current_driver_alias`
- inspect available drivers through `driver[index]`
- inspect supported modes through `driver[index].mode[mode_index]`
- switch driver and mode through `set_graphics_driver(...)`
