# Graphics Text Rendering Plan

## Purpose
Add reusable text drawing and text measurement services for all graphics drivers, with one common API that accepts a position and a font, while keeping the font model abstract enough for future TrueType-style providers.

## Scope
- All graphics backends exposed through the `DF_GFX_*` contract.
- Shared text rendering and measurement logic, not backend-specific one-off code.
- Initial font source: existing console bitmap font.
- Future-compatible font abstraction for scalable/vector-backed fonts.
- High-level userland exposure through windowing/graphics system calls and runtime wrappers.

## Implementation Steps
### Step 1: Introduce a generic font face abstraction
- [x] Define a reusable font interface that separates font identity, metrics, and glyph raster access from the current fixed bitmap glyph-set structure.
- [x] Provide a first adapter backed by the existing console font so the initial implementation reuses the current glyph data without changing behavior.
- [x] Keep the abstraction font-type agnostic so later providers can expose rasterized TrueType-style glyphs through the same contract.

### Step 2: Add a shared graphics text API
- [x] Define one graphics text draw entry point for printing a string at pixel coordinates with an explicit font.
- [x] Define one graphics text measure entry point that returns text width and height for a string with an explicit font.
- [x] Keep line-break, advance, and bounding-box rules centralized in shared code so every backend measures and draws text the same way.
- [x] Define matching high-level kernel entry points that can be exposed safely to userland without leaking backend-private details.

### Step 3: Route all drivers through one renderer
- [x] Implement the new text draw and measure path in shared graphics code, with backends only supplying pixel writes and context synchronization.
- [x] Wire selector, VESA, GOP, and Intel graphics drivers to the same shared implementation so text rendering behavior stays identical across backends.
- [x] Reuse the shared font abstraction in console-adjacent text paths where it makes sense, instead of growing a second text stack.

### Step 4: Compatibility, validation, and documentation
- [ ] Preserve the existing console-font visual result for the first implementation.
- [ ] Expose the high-level text draw and measure API to userland through new syscalls and runtime wrappers using the same font abstraction contract.
- [ ] Validate x86-32 and x86-64 builds and exercise text draw/measure on each active graphics backend.
- [ ] Validate the userland path on window graphics contexts so application-side measurement matches kernel-side rendering.
- [ ] Update `documentation/Kernel.md` with the new graphics text contract and the font abstraction boundaries.
