# Keyboard Layout & HID Roadmap

## Prerequisites

- Define a `KEYCODE` space aligned on HID Usage Page 0x07 (one ID per physical key).
- Add PS/2 Set 1 (and Set 2 if needed) → HID usage translation tables.
- Decide size limits: max modifier levels (base/shift/altgr/ctrl), max compose entries.

## [x] Step 1 - Format & Structures

- Keep legacy PS/2 scan code path (KEYTRANS tables) untouched for compatibility.
- Add a distinct HID layout path (usage page 0x07) that can be used by USB/HID.
- Layout format: UTF-8 text, "EKM1" header, HID usage page 0x07 indexed.
- Directives: code, levels, map, dead, compose, and comments using #.
- map format: map <usage_hex> <level> <vk_hex> <ascii_hex> <unicode_hex>.
- dead format: dead <dead_unicode_hex> <base_unicode_hex> <result_unicode_hex>.
- compose format: compose <first_unicode_hex> <second_unicode_hex> <result_unicode_hex>.
- Limits: levels <= 4, dead keys <= 128, compose entries <= 256.
- Fallback: embedded en-US layout when file load fails.

## [x] Step 2 - Loader/Parser (Kernel)

- Freestanding parser (no stdlib), UTF-8 aware, with bounds checks.
- Validate entries (usage range, level count), log errors, reject malformed directives.
- Expose `LoadKeyboardLayout(path) -> const KEY_LAYOUT_HID*`.

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
