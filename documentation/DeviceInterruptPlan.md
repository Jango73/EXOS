# Device Interrupt Transition Plan

## Context
- The legacy `NetworkManagerTask` polling loop has been replaced by a shared `DeferredWorkDispatcher` that services device interrupts while retaining a polling fallback.
- Polling burns CPU time, delays event handling, and hides interrupt routing issues on both architectures.
- The objective is to establish an interrupt-driven framework for device completions while preserving the ability to fall back to polling during bring-up.
- The same primitives must serve every hardware family (PCI, ISA, on-board controllers, etc.) so future drivers plug into the same infrastructure instead of cloning network-specific plumbing.

## Goals
- Allow device drivers to arm hardware interrupts for completion events once initialization finishes.
- Deliver device interrupts through the existing IOAPIC/LAPIC abstraction without breaking i386 or x86-64 builds.
- Replace ad-hoc polling loops with deferred bottom-half processing that respects current tasking and locking rules.
- Keep diagnostic visibility: maintain controllable polling hooks for debugging.

## Work Breakdown
1. **Audit Current Polling Path**
   - Map every call site reachable from `NetworkManagerTask` and document expected interrupt sources.
   - Capture existing lock ordering and buffer management assumptions before refactoring.
   - Identify additional subsystems (storage, input, timers) that still rely on polling so they can reuse the same infrastructure later.
2. **Interrupt Plumbing**
   - Reserve vector slots for device interrupts in `kernel/source/Interrupt.c` and update descriptor tables.
   - Extend `InterruptController.c` so any device can request routing through IOAPIC or PIC paths.
   - Add driver-level enable/disable helpers so initialization code can arm device interrupts after hardware setup.
3. **Kernel Signaling Primitive**
   - Introduce a lightweight event/semaphore object dedicated to ISR-to-task signaling (none exists today).
   - Provide `Signal`/`Reset` style helpers callable from interrupts and integrate the object with `Wait()`.
   - Expose creation/destruction APIs and tie reference counting into existing kernel object handling.
   - Leave the existing Task message queues untouched; they are not safe for ISR use and stay in user/task space.
4. **Top-Half ISR Implementation**
   - Implement minimal ISRs per device that acknowledge the hardware, mask spurious sources, and signal the new primitive.
   - Use `SAFE_USE_VALID_ID` macros around shared objects accessed from the ISR path.
5. **Deferred Processing (Bottom Half)**
   - Create a generic worker routine `DeferredWorkDispatcher` that runs only when signaled.
   - Replace busy polling with waitable primitives (e.g., the new kernel event) triggered by the ISR.
   - When global configuration forces polling mode, iterate registered poll callbacks instead of sleeping.
   - Ensure device-specific housekeeping reuses existing helper functions instead of duplicating logic.
6. **Generic Deferred Work Module**
   - Move the dispatcher and shared plumbing into a dedicated module (e.g., `kernel/source/system/DeferredWork.c`).
   - Provide headers so subsystems replace `NetworkManagerTask` references with the new generic API.
   - Delete legacy `NetworkManagerTask` once all call sites migrate.
7. **Driver Updates**
   - Update `network/e1000` (and other drivers) to:
     - Program interrupt mask registers.
     - Provide acknowledge routines invoked by the ISR.
     - Remove redundant polling logic while keeping watchdog timers for fault recovery.
   - Replicate the same steps for storage, input, and future device families as they switch away from polling.
8. **Mode Selection & Polling Registry**
   - Introduce a kernel-level configuration value that selects interrupt vs. polling mode at runtime.
   - Use `GetConfigurationValue` like other device settings for kernel parameters.
   - Configuration path for polling/interrupt mode is "General.Polling": 0 = interrupt driven, 1 = polling driven.
   - Fallback polling/interrupt mode value = interrupts (0).
   - Create a generic registration API so any device can enlist poll callbacks consumed by the dispatcher.
   - Preserve existing polling semantics when interrupts are disabled, including per-device cadence or throttling controls.
9. **Maintenance & Fallback**
   - Retain a low-frequency watchdog timer to detect lost interrupts and trigger a one-shot poll.
   - Expose debug controls (e.g., via shell command) to toggle between interrupt and polling modes for troubleshooting.
10. **Testing & Validation**
   - Run `./scripts/i386/4-5-build-debug.sh` and validate boots under QEMU with device traffic (network packets, disk I/O, etc.).
   - Confirm no faults in `log/kernel.log` and that interrupts fire (verify via DEBUG traces with bounded verbosity).
   - Repeat on x86-64 build; compare throughput and CPU usage against baseline polling.
11. **Documentation**
    - Update `documentation/Kernel.md` to describe the interrupt-driven device pipeline.
    - Add driver-specific notes under `documentation/kernel/` detailing ISR flow and configuration knobs.

## Progress Log
- **Step 1 complete**: see `documentation/NetworkInterruptAudit.md` for the initial polling audit, expected interrupt sources, and locking notes.
- **Step 2 complete**: device interrupts use dedicated vectors, IOAPIC/PIC routing helpers, and driver-level enable/disable commands through `DeviceInterruptRegister`.
- **Step 3 complete**: `KernelEvent` now bridges ISRs and tasks and integrates with the scheduler `Wait()` path.
- **Step 4 complete**: the E1000 driver installs a top-half that acknowledges causes, relies on `SAFE_USE_VALID_ID`, and signals deferred work.
- **Step 5 complete**: `DeferredWorkDispatcher` replaces the busy polling loop, driving bottom halves from interrupts or the polling fallback, and keeps network maintenance shared.
- **Step 6 complete**: the dispatcher lives in `kernel/source/system/DeferredWork.c`, headers expose the API, and the legacy `NetworkManagerTask` has been removed.
