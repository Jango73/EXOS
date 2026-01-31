# SMP Implementation Plan

Goal: enable n-core SMP on modern x86 (x86-32 and x86-64) using Local APIC + IOAPIC, with minimal regression for current single-core flow.
Use LAPIC timers when available, fallback to PIT.

---

## Current State (baseline)
- Local APIC driver exists but stays mapped/disabled to avoid PIC conflict; no SMP bring-up.
- ACPI parses MADT and stores Local/IO APIC info; Kernel/interrupt code still assumes a single CPU.
- Scheduler holds one global task array with no CPU affinity and no locking.
- Clock uses PIT (8254) as the only scheduler tick source.

---

## [ ] Step 1 — CPU Discovery and Boot Policy
- Use MADT to enumerate enabled processors; store per-CPU records (ACPI ProcessorId, APIC ID, flags) for both x86-32 and x86-64.
- Validate APIC base from MSR vs ACPI override; refuse SMP if APIC disabled or no MADT.
- Define maximum CPU count (e.g., 32) and error out gracefully when exceeded.
- Success: boot log lists all detected CPUs with APIC IDs and selected BSP.

## [ ] Step 2 — BSP Early Init with APIC Mode
- Enable Local APIC on BSP (IA32_APIC_BASE_MSR | ENABLE, spurious vector set, LVT mask default).
- Transition interrupt mode: PIC masked, IOAPIC initialized, spurious vector configured.
- Prepare LAPIC timer calibration path (PIT or HPET as reference), even if PIT later disabled.
- Success: BSP runs with LAPIC enabled and interrupts routed via IOAPIC.

## [ ] Step 3 — AP Bootstrap Path
- Reuse existing trampoline machinery: copy the already-present real-mode/32-bit bootstrap stub to low memory, but parameterize it for AP startup (stack pointer, CR3, GDT/IDT pointers).
- BSP sends INIT + SIPI to each APIC ID (skip BSP) pointing to the shared trampoline vector.
- In trampoline: set a per-AP temporary stack, load GDT, enable paging with BSP CR3, enable LAPIC, then jump to a dedicated AP C entry.
- AP C entry builds per-CPU stack/TSS, loads IDT, signals “online”, and parks in the idle loop waiting for work.
- Success: every AP executes the trampoline and reaches AP C entry without new bootstrap infrastructure.

## [ ] Step 4 — Per-CPU Structures and Accessors
- Introduce `CPU` struct (PascalCase fields) holding: APIC ID, ACPI ProcessorId, Status (offline/booting/online), PerCpuStack/TSS pointers, CurrentTask, LocalAPIC base, LapicTimer state, PerCpuFlags, Statistics.
- Provide per-CPU storage accessors/macros (CurrentCPU(), PerCPUGet/Set) using GS base on x86-64 and segment-bases or per-CPU array indexed by APIC ID on x86-32.
- Keep a global CPU table and bitmap of online CPUs; initialize BSP entry before AP start.
- Success: any CPU can obtain its CPU struct without global locks.

## [ ] Step 5 — Scheduler/Task Affinity
- Replace single TaskList with per-CPU run queues plus a global work queue (for orphan tasks) or work stealing.
- Add task affinity field (default: ANY); ensure CreateTask pins kernel threads (e.g., idle) to their CPU.
- Implement reschedule IPIs to wake other CPUs when enqueueing to a remote run queue.
- Protect run queues with spinlocks; remove assumptions of uniprocessor Freeze/Unfreeze.
- Success: round-robin or simple priority scheduling works independently on each CPU; tasks can migrate when affinity allows.

## [ ] Step 6 — Interrupts and IPIs
- Allocate vectors for: Reschedule IPI, TLB Shootdown, Generic Call, LAPIC Timer, Error/Spurious.
- Configure IOAPIC redirections to deliver device IRQs to a chosen CPU (e.g., BSP) or lowest-priority mode; keep room for later balance.
- Implement LAPIC EOI paths and per-CPU interrupt statistics.
- Success: IRQs delivered and acknowledged via LAPIC on all CPUs; IPIs received and handled.

## [ ] Step 7 — Timing and Tick Source
- Calibrate LAPIC timer (one-time using PIT or HPET) and switch scheduler tick to per-CPU LAPIC timer; PIT may be disabled afterward.
- Choose a single timekeeper CPU for wallclock increments to avoid double-accounting; share jiffies/system time via atomic ops.
- Optionally add TSC deadline mode when available; otherwise periodic LAPIC timer.
- Success: stable 10 ms scheduler tick on each CPU, consistent uptime/time across CPUs.

## [ ] Step 8 — Synchronization Primitives
- Audit Mutex/Spinlock primitives for multi-CPU correctness; add atomic operations as needed (per Base.h types).
- Protect scheduler, process lists, handle map, and memory allocators with proper locks or per-CPU structures.
- Ensure interrupt disable + spinlock pairs are used in paths reachable from interrupt context.
- Success: no data races when stressing task creation/kill, IPC, file operations across CPUs.

## [ ] Step 9 — Memory and TLB Coherency
- Add TLB shootdown mechanism: track CPUs running a given address space, send shootdown IPIs with vector + sync barrier.
- Ensure CR3 switches are per-CPU; keep per-CPU kernel stacks and IST/ESP0 updated in TSS for each task switch.
- Update page allocator to be SMP-safe (locks or per-CPU caches).
- Success: mapping changes visible on all CPUs; no stale TLB usage.

## [ ] Step 10 — Boot/Shutdown Flow and Debug
- Adjust boot sequencing: ACPI → APIC init → CPU bring-up → scheduler start; ensure debug output uses CPU ID prefix.
- Provide CPU offline/shutdown hook (at least halt APs) for clean reboot.
- Update `documentation/Kernel.md` after component changes.
- Success: clean boot logs and orderly halt/reboot with multiple CPUs.

## [ ] Step 11 — Testing Matrix
- QEMU: run with `-smp 4` on both x86-32 and x86-64; verify all CPUs online, tasks scheduled, no faults in `log/kernel.log`.
- Stress: spawn many tasks, IPC, file ops; verify no deadlocks/races.
- Timer: measure tick accuracy before/after switching to LAPIC timer.
- Interrupts: confirm device IRQ delivery (keyboard/net) and IPI handling.

---

## Risk Mitigation / Compatibility
- Keep a uniprocessor fallback path (boot with `nosmp` flag) until SMP is stable.
- If PIT compatibility is costly, retire PIT after LAPIC timer is enabled and calibrated.
- Guard new vectors to avoid conflicts with existing IRQ layout; document vector map.
