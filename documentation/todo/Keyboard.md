# Keyboard Layout & HID Roadmap

## Prerequisites

- Define a `KEYCODE` space aligned on HID Usage Page 0x07 (one ID per physical key).
- Add PS/2 Set 1 (and Set 2 if needed) → HID usage translation tables.
- Decide size limits: max modifier levels (base/shift/altgr/ctrl), max compose entries.

## [ ] Step 1 — Format & Structures

- Pick the layout file format (HID-indexed, optionally imported from `.kmap`) and allowed directives.
- Define kernel structures (`KEY_LAYOUT`, compose table, dead-key markers).
- Specify fallback embedded layout when file load fails.

## [ ] Step 2 — Loader/Parser (Kernel)

- Freestanding parser (no stdlib), UTF-8 aware, with bounds checks.
- Validate entries (usage range, level count), log errors, reject malformed files.
- Expose `LoadKeyboardLayout(path) -> const KEY_LAYOUT*`.

## [ ] Step 3 — Input Pipeline Refactor

- `Key.c` consumes HID usages + modifier state instead of raw PS/2 scancodes.
- PS/2 driver emits HID usages into a normalized path.
- Layout mapping applies (dead/compose, repeat handling).

## [ ] Step 4 — USB Keyboard Ready Hook

- Entry point for USB HID driver to push HID usages into the same pipeline.
- Document HID report parsing (boot protocol first).
- Ensure PS/2 and USB coexist (shared normalized path).

## [ ] Step 5 — Tooling & Baseline Layouts

- Offline converter `.kmap` (Linux console) → internal HID-indexed format.
- Baseline layout files: us, fr, de, es (+ minimal embedded fallback).
- Boot/config switch to select layout (e.g., `keyboard.layout=fr`).

## [ ] Step 6 — Documentation & Tests

- Update `documentation/Kernel.md` with transport vs mapping architecture and config.
- Minimal keyboard self-test: usages + modifiers → expected codepoints.
- How to add/validate a new layout file.
