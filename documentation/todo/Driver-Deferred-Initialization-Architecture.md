# Driver Deferred Initialization Architecture

## Goal

Introduce a global deferred initialization mechanism for machine-component drivers.

The target is to replace ad-hoc polling/retry patterns with:

- explicit dependency signals
- centralized deferred queue management
- deterministic retry and observability

## Design Principles

- Keep the existing `DF_LOAD` contract and driver list order.
- Add a kernel service for dependency wait/retry orchestration.
- Avoid per-driver custom defer logic when a shared mechanism can handle it.
- Support both event-driven wakeup and polling fallback.

## Core Concept

A driver can return a dedicated status (for example `DF_RETURN_DEFERRED`) and provide:

- what it is waiting for (dependency signals enum)
- optional timeout and retry hints

The kernel stores deferred entries in a global queue and retries them when:

- required signals are emitted
- or periodic fallback retry triggers

## Proposed Data Model

## 1) Dependency Signal Enum

Define a global enum for initialization dependencies, for example:

- `DRIVER_DEP_INTERRUPTS_READY`
- `DRIVER_DEP_DEVICE_INTERRUPT_READY`
- `DRIVER_DEP_DEFERRED_WORK_READY`
- `DRIVER_DEP_PCI_READY`
- `DRIVER_DEP_XHCI_READY`
- `DRIVER_DEP_XHCI_ENUM_READY`
- `DRIVER_DEP_FILESYSTEM_READY`

Notes:

- Use a bitmask representation (`U64` or `U128`) for fast checks.
- Keep this enum kernel-global, not per-driver.

## 2) Driver Deferred State

Add state in a dedicated kernel-owned structure (recommended), keyed by driver pointer.

Suggested fields:

- `PendingDepsMask`
- `SatisfiedDepsMaskSnapshot`
- `RetryCount`
- `NextRetryTime`
- `FirstDeferredTime`
- `LastAttemptTime`
- `LastResultCode`
- `LastReasonCode`
- `State` (`IDLE`, `DEFERRED`, `RUNNING`, `READY`, `FAILED`)

Recommendation:

- Keep runtime defer bookkeeping outside `DRIVER` base structure unless footprint constraints are strict.

## 3) Global Deferred Queue

A kernel list or ring storing deferred init entries:

- `Driver`
- `PendingDepsMask`
- `NextRetryTime`
- `Priority`
- `Flags` (event-driven eligible, poll fallback enabled)

## Kernel APIs (Minimal)

## 1) Signal API

- `DriverInitSignalEmit(DRIVER_INIT_SIGNAL Signal)`
- `DriverInitSignalGetMask(void)`
- `DriverInitSignalIsSet(DRIVER_INIT_SIGNAL Signal)`

## 2) Deferred Registration API

- `DriverInitDefer(LPDRIVER Driver, U64 PendingDepsMask, UINT ReasonCode, UINT RetryDelayMs)`
- `DriverInitContinue(LPDRIVER Driver)` (retry execution entry)
- `DriverInitCancel(LPDRIVER Driver)`

## 3) Introspection API

- `DriverInitGetDeferredList(...)`
- `DriverInitGetDriverState(LPDRIVER Driver, ...)`

## Driver Contract Extension

Keep `DF_LOAD`, add one defer return path:

- `DF_RETURN_SUCCESS`: ready
- `DF_RETURN_DEFERRED`: not ready yet, defer with dependency mask
- existing error codes: hard failure

Per-driver pattern:

1. Check hard prerequisites.
2. If missing, call `DriverInitDefer(...)` and return `DF_RETURN_DEFERRED`.
3. If available, continue normal init.
4. On success, set `DRIVER_FLAG_READY`.

## Trigger Model

Use hybrid wakeup:

- event-driven:
  - when a subsystem becomes ready (`PCI`, `xHCI`, filesystem), emit signal
  - deferred manager retries matching entries immediately
- timer fallback:
  - periodic scan retries entries whose `NextRetryTime` elapsed

This avoids deadlocks from missing emits and reduces useless busy polling.

## Suggested Integration Points

## Signal emitters

- end of successful `DF_LOAD` for core providers:
  - `InterruptController`
  - `DeviceInterrupt`
  - `DeferredWork`
  - `PCI`
  - `xHCI` attach/enumeration milestones
  - filesystem readiness transition

## Typical consumers

- `Keyboard-USB`, `Mouse-USB`, `USBMassStorage`
- storage/network drivers requiring interrupt stack availability

## State Machine Recommendation

Use explicit init stages for complex drivers:

- `STAGE_ALLOC`
- `STAGE_BUS_BIND`
- `STAGE_IRQ_SETUP`
- `STAGE_IO_SETUP`
- `STAGE_ENUM`
- `STAGE_READY`

This prevents full re-execution on every deferred retry.

## Observability Requirements

Add shell/debug visibility:

- list deferred drivers
- pending dependency mask
- retry count
- age since first defer
- next retry time
- last error/reason

Suggested shell command:

- `driver defer list`

## Step-by-Step Implementation Plan

1. Define base constants and types.
- Add `DF_RETURN_DEFERRED`.
- Add `DRIVER_INIT_SIGNAL` enum and mask helpers.
- Add reason codes enum for defer diagnostics.

2. Implement kernel signal registry.
- Global signal bitmask.
- `Emit/Get/IsSet` APIs.
- thread-safe updates.

3. Implement deferred queue manager module.
- new files in kernel utils/core area for reuse.
- queue add/update/remove.
- retry scheduling metadata.

4. Add deferred manager task/tick integration.
- event wakeup hook (on signal emit).
- periodic fallback check using existing timing infrastructure.

5. Extend driver load path.
- in `LoadDriver`, treat `DF_RETURN_DEFERRED` as non-failure.
- enqueue or update deferred entry.
- keep critical panic behavior only for hard failures.

6. Add producer emits in core drivers.
- emit readiness signals on successful provider initialization.

7. Migrate first consumer set.
- migrate `Keyboard-USB`, `Mouse-USB`, `USBMassStorage` to return deferred instead of private retry-only startup gating.

8. Add staged init for one complex driver.
- implement state-machine retry continuation.
- validate reduced rework on retries.

9. Add introspection and diagnostics.
- expose deferred queue to shell/debug.
- include dependency and reason decoding.

10. Validation matrix.
- boot with normal order.
- boot with intentionally delayed providers.
- polling mode on/off.
- verify no permanent stuck deferred entry without diagnostics.

11. Optional hardening.
- max retry policy + terminal failure state.
- per-driver backoff profiles.
- boot report summary for deferred/failed drivers.

## Compatibility Strategy

- Existing drivers continue to work unchanged.
- New mechanism is opt-in per driver.
- Migration can proceed subsystem by subsystem.

## Expected Benefits

- Cleaner dependency handling between machine-component drivers.
- Less duplicated polling/retry code in drivers.
- Better boot determinism under partial readiness conditions.
- Better diagnostics for initialization stalls.
