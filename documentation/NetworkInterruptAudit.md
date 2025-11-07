# Network Interrupt Audit

## Polling Call Graph
- `Kernel.c` schedules `NetworkManagerTask` during boot (task creation near line 1119).
- `NetworkManagerTask` ( `kernel/source/network/NetworkManager.c` ) loops forever:
  - Iterates `Kernel.NetworkDevice` and calls `Driver->Command(DF_NT_POLL)` on each attached NIC.
  - Every 100 iterations, calls `ARP_Tick`, `DHCP_Tick`, `TCP_Update`, and `SocketUpdate` for maintenance.
- Driver `DF_NT_POLL` handlers dispatch to hardware-specific receive logic (e1000 uses `E1000_OnPoll`).

## Expected Interrupt Sources
- Intel E1000 hardware raises:
  - `RXDMT0` when descriptors drop below threshold.
  - `RXO` on receive overruns.
  - `RXT0` as receive timer watchdog.
  - `TXDW` and `TXQE` for transmit completion/queue empty (currently unused).
  - `LSC` (link status change) on carrier transitions.
- All map to the legacy INTx line reported in `PCI_INFO.IRQLine` (usually IRQ 11 in QEMU) and should be routed to the new network interrupt vector.

## Locking and Buffer Assumptions
- `NetworkManagerTask` performs no explicit locking while iterating devices; it relies on `SAFE_USE`/`SAFE_USE_VALID_ID` to validate list membership without holding `MUTEX_KERNEL`.
- RX/TX rings in `E1000.c` are backed by statically allocated pools (128 descriptors each) sized to one page per ring.
- `E1000_SetupReceive` primes every descriptor and advances `RDT` after returning the buffer to hardware; transmit path blocks when `(TDT + 1) % TX_DESC_COUNT == TDH`.
- No shared state is touched inside ISRs yet; polling code assumes single-threaded access from the manager task.

## Identified Follow-Up Requirements
- Any ISR must acknowledge `ICR`, refill RX descriptors, and signal deferred work without grabbing heavy locks.
- Shared data between ISR and task needs a new kernel event/semaphore primitive; existing task message queues remain unsuitable for ISR context.
