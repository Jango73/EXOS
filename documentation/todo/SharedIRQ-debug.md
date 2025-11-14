/************************************************************************\

    Shared IRQ 11 Debug Notes

\************************************************************************/

## Context

- Commit `2f8fe44e1cb2a9d41bd46a5100aa1646583f5620` introduced the new device interrupt dispatcher (vectors 0x30–0x37).
- On x86-64 the legacy INTx line for the `E1000` NIC is IRQ 11. The AHCI controller is also wired to IRQ 11 inside QEMU (`PCI_INFO.IRQLine` reads 11 for both devices).
- The existing AHCI driver never handled interrupts (it polls synchronously) but still enabled both per-port (`PORT.ie`) and global (`GHC.IE`) interrupt enables. This caused a constant IRQ storm as soon as the driver reset the controller.

## Symptoms

- `log/kernel.log` spam of:
  - `[E1000_AcknowledgeInterrupt] Cause=0x0 Armed=YES Polling=NO`
  - `[E1000_InterruptTopHalf] No cause reported (trace=N)`
  - `[DeviceInterruptHandler] Slot=0 IRQ=11 handler suppressed signal while IRQ still armed`
- System responsiveness collapsed in x86-64 builds because the dispatcher was invoked continuously.

## Fix

1. **Make AHCI silent.**
   - In `kernel/source/drivers/SATA.c`:
     - During `InitializeAHCIPort`, clear pending status (`Port->is = 0xFFFFFFFF`) and leave `Port->ie = 0x0`.
     - After enumerating ports in `InitializeAHCIController`, keep the global `GHC.IE` bit cleared.
   - Rationale: SATA commands are completed synchronously, so no legitimate interrupt is needed. Masking the controller prevents bogus signals on the shared line.

2. **Expose interrupt routing in logs.**
   - `kernel/source/drivers/PCI.c` now prints the legacy IRQ line when enumerating devices. Example:
     ```
     [PCI] Found 0x0:0x3.0 VID=0x8086 DID=0x100e IRQ=11
     ```
   - This helps detect shared IRQs immediately.

3. **Document the policy.**
   - `documentation/Kernel.md` records that AHCI keeps IRQ 11 quiet so the `E1000` slot can operate without storms.

## Verification Checklist

1. **Build x86-64 debug image**
   ```bash
   ./scripts/x86-64/4-5-build-debug-ext2.sh
   ```
2. **Launch QEMU headless (15 s budget)**
   ```bash
   timeout 15 qemu-system-x86_64 -display none \
     -machine q35,acpi=on,kernel-irqchip=split \
     -smp 1 \
     -device ahci,id=ahci \
     -drive format=raw,file="build/x86-64/boot-hd/exos.img",if=none,id=drive0 \
     -device ide-hd,drive=drive0,bus=ahci.0 \
     -netdev user,id=net0 \
     -device e1000,netdev=net0 \
     -object filter-dump,id=dump0,netdev=net0,file=log/kernel-net.pcap \
     -serial file:log/debug-com1.log \
     -serial stdio -no-reboot 2>&1 | \
     build/x86-64/tools/cycle -o log/kernel.log -s 200000
   ```
3. **Inspect log**
   - Expect exactly two `[DeviceInterruptHandler]` entries when DHCP frames arrive, both followed by `[E1000_InterruptTopHalf] Cause=0x80/0x83`.
   - No `[E1000_AcknowledgeInterrupt] Cause=0x0 …` or `[DeviceInterruptHandler] … suppressed signal …` warnings.
   - SATA section of the log shows initialization but **no** AHCI interrupt activity.

4. **Optional sanity check**
   - `rg -n "IRQ=" log/kernel.log` lists each enumerated device and its IRQ.

## Future Work

- If AHCI needs real interrupt-driven I/O, either:
  - Route it through `DeviceInterruptRegister` with its own slot and proper top/bottom halves, or
  - Enable MSI/MSI-X so it no longer shares IRQ 11 with E1000.
- Update `SATA.c` to re-enable interrupts only after the driver implements handlers.

