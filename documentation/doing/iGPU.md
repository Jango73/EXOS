# Intel iGPU Driver Roadmap (native, windowing-focused)

## Scope
- Build a native Intel integrated graphics driver for display and 2D windowing.
- Reuse the existing graphics stack (`kernel/include/GFX.h`, windowing already present).
- No 3D API, no shader pipeline, no render command submission.
- Support model differences through explicit capabilities, not ad-hoc per-model branching.

## Non-goals
- No OpenGL/Vulkan/Direct3D equivalent.
- No media decode/encode blocks.
- No overclocking or advanced power tuning.

## Design Rules
- Keep one generic graphics API and one Intel backend.
- Prefer capability-driven behavior over hardcoded model checks.
- Keep source files split by responsibility (under 1000 lines each).
- Every unimplemented driver command returns `DF_RETURN_NOT_IMPLEMENTED`.
- Keep logs actionable and rate-limited for polling paths.

## Step 0 - Baseline and contract freeze
- [x] Record exactly which `DF_GFX_*` commands are used by Desktop/Console/SYSCall.
- [x] Document what `VESA.c` already guarantees (mode set, linear memory access, pixel ops).
- [x] Define the minimal contract required by your window manager to be backend-agnostic.

Deliverable:
- A short compatibility note: "what every graphics driver must provide to run the window manager".
- Delivered in `documentation/doing/iGPU-Step0-Compatibility.md`.

## Step 1 - Evolve `GFX.h` into a stable backend interface
The existing API is enough for software drawing, but too poor for clean native scanout management.

- [x] Add capability and output query commands:
  - `DF_GFX_GETCAPABILITIES`
  - `DF_GFX_ENUMOUTPUTS`
  - `DF_GFX_GETOUTPUTINFO`
- [x] Add present/synchronization commands:
  - `DF_GFX_PRESENT`
  - `DF_GFX_WAITVBLANK`
- [x] Add optional page-flip support:
  - `DF_GFX_ALLOCSURFACE`
  - `DF_GFX_FREESURFACE`
  - `DF_GFX_SETSCANOUT`
- [x] Keep existing commands (`SETPIXEL`, `LINE`, `RECTANGLE`) for compatibility.
- [x] Require legacy drivers (VESA) to return `DF_RETURN_NOT_IMPLEMENTED` for new optional commands.

Suggested new structures:
- `GFX_CAPABILITIES`
  - `HasHardwareModeset`
  - `HasPageFlip`
  - `HasVBlankInterrupt`
  - `HasCursorPlane`
  - `SupportsTiledSurface`
  - `MaxWidth`, `MaxHeight`
  - `PreferredFormat`
- `GFX_OUTPUT_INFO`
  - `OutputId`
  - `Type` (eDP, HDMI, DisplayPort, VGA)
  - `IsConnected`
  - `NativeWidth`, `NativeHeight`, `RefreshRate`
- `GFX_SURFACE_INFO`
  - `Width`, `Height`, `Format`, `Pitch`, `MemoryBase`, `Flags`

Deliverable:
- `GFX.h` update proposal that stays backward-compatible with VESA.
- Implemented in `kernel/include/GFX.h` and `kernel/source/drivers/graphics/VESA.c`.

## Step 2 - Intel driver skeleton and PCI attach
Create a dedicated Intel graphics driver module.

- [x] Add `kernel/source/drivers/graphics/IntelGfx.c` as entry/dispatch.
- [x] Detect Intel GPU on PCI (`VendorId = 0x8086`, display class).
- [x] Enable MMIO + bus master on PCI command register.
- [x] Read BARs and map the MMIO BAR with `MapIOMemory`.
- [x] Validate MMIO access through a harmless identity register read.

Deliverable:
- Driver loads, identifies Intel GPU, maps MMIO, prints concise identification logs.
- Delivered by `IntelGfx.c`, with backend selection/fallback managed by `Graphics-Selector.c`.

## Step 3 - Capability model by generation
Avoid branching everywhere by centralizing capabilities.

- [x] Add `INTEL_GFX_CAPS` and fill it at init from:
  - PCI device id family table.
  - Key register probes (display version, pipe/port presence).
- [x] Expose normalized values to generic layer through `DF_GFX_GETCAPABILITIES`.

Example capability fields:
- `Generation`
- `DisplayVersion`
- `PipeCount`
- `TranscoderCount`
- `PortMask`
- `SupportsFBC`
- `SupportsPSR`
- `SupportsAsyncFlip`

Deliverable:
- One capability object that drives all later code paths.
- Implemented in `kernel/source/drivers/graphics/IntelGfx.c` with table-based defaults + MMIO probes.

## Step 4 - Mode takeover path (first usable milestone)
First milestone should avoid full native modesetting complexity.

- [ ] Read active scanout state from Intel display registers (pipe, plane, stride, base).
- [ ] Build a `GRAPHICSCONTEXT` from the active mode.
- [ ] Map the active framebuffer memory and expose it as `MemoryBase`.
- [ ] Present through CPU blit to active scanout buffer.

Why this step:
- Produces a native Intel backend with minimal risk.
- Gives immediate value for your existing windowing pipeline.

Deliverable:
- Desktop can draw through Intel backend using existing graphics API.

## Step 5 - Native modeset (controlled expansion)
After takeover works, add explicit mode programming.

- [ ] Implement mode validation (`width/height/format/refresh` against capabilities).
- [ ] Implement pipe disable/enable sequence.
- [ ] Program timings, stride, surface base, pixel format.
- [ ] Bring panel/backlight handling only where needed for internal panel stability.
- [ ] Keep one conservative mode path first (for example: one pipe, one output).

Deliverable:
- `DF_GFX_SETMODE` programs Intel display pipeline without firmware fallback.

## Step 6 - Buffer management and present model
Introduce a clean surface model for future growth.

- [ ] Implement software front/back surface allocation in driver-controlled memory.
- [ ] Add `DF_GFX_PRESENT` semantics:
  - If page flip supported: flip.
  - Else: blit dirty regions.
- [ ] Keep a generic dirty-rectangle input path for the window manager.

Deliverable:
- Flicker-free present path with clear fallback behavior.

## Step 7 - VBlank and synchronization
- [ ] Add optional vblank interrupt handling.
- [ ] Implement `DF_GFX_WAITVBLANK` with timeout safety.
- [ ] Protect present path with lightweight locking + frame sequence counters.

Deliverable:
- Stable frame pacing and reduced tearing on supported configurations.

## Step 8 - Output management (multi-output ready)
- [ ] Implement `ENUMOUTPUTS` and `GETOUTPUTINFO`.
- [ ] Start with one active output policy; keep data model ready for many outputs.
- [ ] Add connector hotplug detection only after single-output stability.

Deliverable:
- Output enumeration API stable even before full multi-monitor policy is enabled.

## Step 9 - Integration with existing VESA backend
- [ ] Keep VESA as fallback backend.
- [ ] Ensure Desktop chooses backend by capability/priority policy.
- [ ] Add a boot option or kernel config key to force one backend for debugging.

Deliverable:
- Safe fallback path and deterministic backend selection.

## Step 10 - Diagnostics and shell tooling
- [ ] Add concise commands (or debug paths) to print:
  - GPU identification and capabilities
  - Active pipe/plane/output
  - Current mode and stride
  - Present path stats (flip/blit counters, suppressed warnings)
- [ ] Use shared rate limiting in polling loops.

Deliverable:
- Repeatable diagnostics for bring-up and regression tracking.

## Step 11 - Test matrix and acceptance gates
Minimum gates before considering the driver stable:

- [ ] Boot x86-32 and x86-64 with Intel backend enabled.
- [ ] Desktop starts and draws correctly.
- [ ] 15-second stress loop of window moves/invalidations without fault.
- [ ] Mode set success and rollback behavior validated.
- [ ] Suspend/resume not required for first stable release unless explicitly targeted.

## Suggested code split
Keep one file per concern:
- `IntelGfx.c` (driver entry, dispatch, lifecycle)
- `IntelGfxPci.c` (PCI detection/attach)
- `IntelGfxCaps.c` (capability model)
- `IntelGfxMode.c` (mode takeover + native modeset)
- `IntelGfxSurface.c` (surface allocation/present)
- `IntelGfxInterrupt.c` (vblank/interrupts)
- `IntelGfxOutput.c` (connector/output info)

## Suggested incremental milestones
1. `M1`: Intel detect + MMIO map + capability dump.
2. `M2`: Mode takeover + desktop draw path works.
3. `M3`: Native `SETMODE` for one output.
4. `M4`: Present with page-flip fallback model.
5. `M5`: VBlank sync + output enumeration.

## Notes for PH317-52 target
- Expect hybrid graphics platform behavior (integrated graphics + GeForce present).
- Prioritize internal panel path on Intel backend first.
- Treat discrete GeForce as independent future backend/offload topic.

## Follow-up documentation updates
When implementation starts (not only planning), update:
- `documentation/Kernel.md` with the Intel graphics backend architecture.
- Doxygen pages for new driver modules and exported `GFX.h` structures.
