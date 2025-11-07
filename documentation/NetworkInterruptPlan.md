# Network Interrupt Transition Plan

## Context
- `NetworkManagerTask` in `kernel/source/network/NetworkManager.c` currently polls network devices from a kernel task.
- Polling burns CPU time, delays packet handling, and hides interrupt routing issues on both architectures.
- The objective is to switch to interrupt-driven Rx/Tx completion while preserving the ability to fall back to polling during bring-up.

## Goals
- Configure supported NIC drivers (starting with e1000) to raise interrupts for receive, transmit, and link events.
- Deliver network interrupts through the existing IOAPIC/LAPIC abstraction without breaking i386 or x86-64 builds.
- Replace the polling loop with deferred bottom-half processing that respects current tasking and locking rules.
- Keep diagnostic visibility: maintain controllable polling hooks for debugging.

## Work Breakdown
1. **Audit Current Polling Path**
   - Map every call site reachable from `NetworkManagerTask` and document expected interrupt sources.
   - Capture existing lock ordering and buffer management assumptions before refactoring.
2. **Interrupt Plumbing**
   - Reserve vector(s) for NIC interrupts in `kernel/source/Interrupt.c` and update descriptor tables.
   - Extend `InterruptController.c` to route the NIC IRQ to a sane CPU target on both architectures.
   - Add driver-level enable/disable helpers so initialization code can arm NIC interrupts after hardware setup.
3. **Kernel Signaling Primitive**
   - Introduce a lightweight event/semaphore object dedicated to ISR-to-task signaling (none exists today).
   - Provide `Signal`/`Reset` style helpers callable from interrupts and integrate the object with `Wait()`.
   - Expose creation/destruction APIs and tie reference counting into existing kernel object handling.
   - Leave the existing Task message queues untouched; they are not safe for ISR use and stay in user/task space.
4. **Top-Half ISR Implementation**
   - Implement a minimal ISR per NIC that acknowledges the device, masks spurious sources, and signals the new primitive.
   - Use `SAFE_USE_VALID_ID` macros around shared objects accessed from the ISR path.
5. **Deferred Processing (Bottom Half)**
   - Create a generic worker routine `DeferredWorkDispatcher` that runs only when signaled.
   - Replace busy polling with waitable primitives (e.g., the new kernel event) triggered by the ISR.
   - When global configuration forces polling mode, iterate registered poll callbacks instead of sleeping.
   - Ensure transmit completion, receive ring refill, and link monitoring reuse existing helper functions.
6. **Generic Deferred Work Module**
   - Move the dispatcher and shared plumbing into a dedicated module (e.g., `kernel/source/system/DeferredWork.c`).
   - Provide headers so subsystems replace `NetworkManagerTask` references with the new generic API.
   - Delete legacy `NetworkManagerTask` once all call sites migrate.
7. **Driver Updates**
   - Update `network/e1000` (and other drivers) to:
     - Program interrupt mask registers.
     - Provide acknowledge routines invoked by the ISR.
     - Remove redundant polling logic while keeping watchdog timers for fault recovery.
8. **Mode Selection & Polling Registry**
   - Introduce a kernel-level configuration value that selects interrupt vs. polling mode at runtime.
   - Use `GetConfigurationValue` like other network settings for kernel parameters.
   - Configuration path for polling/interrupt mode is "General.Polling" : 0 = interrupt driven, 1 = polling driven
   - Fallback polling/interrupt mode value = interrupts (0).
   - Create a generic registration API so any device can enlist poll callbacks consumed by the dispatcher.
   - Preserve existing polling semantics when interrupts are disabled, including per-device cadence or throttling controls.
9. **Maintenance & Fallback**
   - Retain a low-frequency watchdog timer to detect lost interrupts and trigger a one-shot poll.
   - Expose debug controls (e.g., via shell command) to toggle between interrupt and polling modes for troubleshooting.
   - Maintain per-driver interrupt and drop counters; emit bounded DEBUG logs (e.g., every N interrupts) to track regressions without flooding output.
10. **Testing & Validation**
   - Run `./scripts/i386/4-5-build-debug.sh` and validate boots under QEMU with packet traffic.
   - Confirm no faults in `log/kernel.log` and that interrupts fire (verify via DEBUG traces with bounded verbosity).
   - Repeat on x86-64 build; compare throughput and CPU usage against baseline polling.
11. **Documentation**
    - Update `documentation/Kernel.md` to describe the interrupt-driven network pipeline.
    - Add driver-specific notes under `documentation/kernel/` detailing ISR flow and configuration knobs.

## Progress Log
- **Step 1 complete**: see `documentation/NetworkInterruptAudit.md` for the polling audit, expected interrupt sources, and locking notes.
- **Step 2 in progress**: network interrupts now use a dedicated vector, IOAPIC/PIC routing helpers, and driver-level enable/disable commands.
- **Step 3 in progress**: a new kernel event object provides ISR-safe signaling with `SignalKernelEvent`/`ResetKernelEvent`, and `Wait()` observes the new object type.
