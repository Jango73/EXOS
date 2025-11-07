# Network Interrupt Audit

## Deferred Work Flow
- `Kernel.c` initializes `DeviceInterrupts` and starts `DeferredWorkDispatcher` (`kernel/source/DeferredWork.c`) instead of the legacy `NetworkManagerTask` loop.
- The dispatcher waits on the shared kernel event, invoking registered device slots when interrupts fire and running poll callbacks on timeout.
- NIC drivers (e.g., `E1000`) register with `DeviceInterruptRegister`, supplying ISR top halves plus deferred and poll routines.
- When hardware interrupts are unavailable or configuration forces polling, the dispatcher keeps executing the poll routines so receive paths and maintenance still advance.

## Expected Interrupt Sources
- Intel E1000 hardware raises:
  - `RXDMT0` when descriptors drop below threshold.
  - `RXO` on receive overruns.
  - `RXT0` as receive timer watchdog.
  - `TXDW` and `TXQE` for transmit completion/queue empty (currently unused).
  - `LSC` (link status change) on carrier transitions.
- All map to the legacy INTx line reported in `PCI_INFO.IRQLine` (usually IRQ 11 in QEMU) and should be routed to the new network interrupt vector.

## Locking and Buffer Assumptions
- ISR top halves rely on `SAFE_USE_VALID_ID` to guard shared objects before signalling deferred work.
- RX/TX rings in `E1000.c` are backed by statically allocated pools (128 descriptors each) sized to one page per ring.
- `E1000_SetupReceive` primes every descriptor and advances `RDT` after returning the buffer to hardware; transmit path blocks when `(TDT + 1) % TX_DESC_COUNT == TDH`.
- Deferred work executes on the dispatcher task context, so shared state still observes task-level serialization without explicit `MUTEX_KERNEL` usage inside driver callbacks.

## Identified Follow-Up Requirements
- Any ISR must acknowledge `ICR`, refill RX descriptors, and signal deferred work without grabbing heavy locks.
- Shared data between ISR and task needs a new kernel event/semaphore primitive; existing task message queues remain unsuitable for ISR context.
