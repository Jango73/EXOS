# VMD Implementation Roadmap

## Prerequisites
- [ ] PCI config access working (bus/dev/fn scan beyond bus 0).
- [ ] MMIO mapping for PCI BARs.
- [ ] Basic PCIe bridge handling (secondary/subordinate buses).
- [ ] Interrupt routing for PCIe (MSI/MSI-X preferred, INTx fallback).
- [ ] Logging: concise traces (enable/disable, bus routing, error codes).

## Step 0 — Identify VMD on PCI
Goal: detect the VMD controller reliably.  
- [ ] Match Intel VMD devices by class 0x06 (bridge) and known IDs (VID=0x8086).  
- [ ] Capture bus/dev/fn, class/subclass/progIF, and BARs.  
Success: `vmd info` (shell command) or System Data View reports VMD presence and location.

## Step 1 — Map VMD MMIO
Goal: read VMD registers and confirm base address.  
- [ ] Map the primary BAR and read version/status registers.  
- [ ] Verify register access is stable (no faults, sensible values).  
Success: `vmd regs` prints basic register fields.

## Step 2 — Enable VMD and Discover the Domain
Goal: expose the PCIe domain behind VMD.  
- [ ] Enable VMD in its control registers (per spec).  
- [ ] Read VMD-provided bus numbers or root-port mapping.  
- [ ] Build a list of downstream buses controlled by VMD.  
Success: VMD reports one or more downstream buses ready to scan.

## Step 3 — PCIe Bridge Enumeration Behind VMD
Goal: enumerate all devices behind VMD.  
- [ ] For each downstream bus, scan PCIe devices.  
- [ ] Handle PCIe bridges: read secondary/subordinate bus numbers, update as needed.  
- [ ] Expose discovered devices via the normal PCI device list.  
Success: NVMe controllers appear in `PCI Devices` even without NVMe driver.

## Step 4 — Interrupt Delivery (MSI/MSI-X)
Goal: make downstream device interrupts work.  
- [ ] Determine VMD interrupt model (MSI/MSI-X routing).  
- [ ] Map VMD interrupt vectors to downstream devices.  
- [ ] Verify interrupt delivery using a known device (e.g., NVMe CQ interrupt).  
Success: downstream device interrupts fire correctly.

## Step 5 — Power Management and Reset
Goal: stable recovery paths.  
- [ ] Implement VMD reset sequence.  
- [ ] Handle device power states and link states for VMD root ports.  
- [ ] Ensure devices re-enumerate after reset.  
Success: `vmd reset` restores the device list.

## Step 6 — Hotplug / Surprise Removal (Optional)
Goal: handle dynamic changes.  
- [ ] Detect link-up/link-down on VMD ports.  
- [ ] Add/remove devices from PCI list safely.  
Success: hotplug events do not crash the system.

## Step 7 — Error Handling & Telemetry
Goal: robust diagnostics.  
- [ ] Decode VMD error/status registers.  
- [ ] Throttle logs and expose error counters.  
Success: errors are visible and non-fatal where possible.

## Step Decoupling (each step runs without the next)
- 0–1: detection only, safe to ship.  
- 2–3: enumeration behind VMD without NVMe support.  
- 4: interrupts, required for full device operation.  
- 5: reset and recovery.  
- 6–7: reliability and maintenance.

## Notes
- Register layouts and enable sequences must follow the Intel VMD specification.  
- Keep the driver minimal: its primary job is to expose the PCIe domain behind VMD.  
- NVMe support remains a separate driver (see `documentation/todo/NVMe.md`).
