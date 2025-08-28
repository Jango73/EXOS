# Incremental PCIe Support Plan for EXOS

This plan extends the existing PCI bus manager in `PCI.c` with minimal disruption, while adding PCIe functionality.

---

## Step 0 — Current State (already implemented)
- Config space access via ports `0xCF8/0xCFC` (`PCI_Read*` / `PCI_Write*`).
- Bus/device/function scan, driver match/attach.
- BAR decoding (I/O vs MEM).
- Classical capability list (offset 0x34, `PCI_FindCapability`).
- Enable bus master flag.

These are preserved. PCIe will be added as thin layers: ECAM, PCIe Capability, MSI/MSI-X, link info.

---

## Step 1 — ECAM (MMCONFIG)
**Goal:** access extended config space (0x100..0xFFF).  
- Locate MMCONFIG base (ACPI MCFG table).  
- Address calculation: `ECAM + (Bus<<20) + (Dev<<15) + (Fn<<12) + Offset`.  
- Policy: if bus covered by ECAM → use MMIO; otherwise fallback to ports.  
**Success:** `pcictl ecam-test` reads beyond 0xFF on a PCIe device.

---

## Step 2 — PCIe Capability (ID 0x10)
**Goal:** detect PCIe functions and expose link/device info.  
- Walk capability list to find **PCIe Cap (0x10)**.  
- Read: Device/Port Type, Link Cap/Status, Max Payload Size, Max Read Request, Slot Cap.  
- Store into `PCIE_INFO` attached to each `PCI_DEVICE`.  
**Success:** `pcictl pcie` prints negotiated Gen, width, payload, MRRS.

---

## Step 3 — MSI / MSI-X Helpers
**Goal:** let drivers request modern interrupts.  
- Add `PCI_EnableMSI(PciDev, VectorCount)` and `PCI_EnableMSIX(PciDev, MapFunc, Count)`.  
- Fallback: MSI-X → MSI → legacy INTx.  
**Success:** a driver can request MSI-X vectors and receive interrupts.

---

## Step 4 — Payload / MRRS Policies
**Goal:** improve throughput.  
- Add setters: `PCIe_SetMaxPayload()`, `PCIe_SetMaxReadRequest()`.  
- Respect negotiated link and root limits.  
**Success:** larger transfers for heavy devices, no regression if not supported.

---

## Step 5 — Stable API to Drivers
**Goal:** expose PCIe info without breaking PCI API.  
- Add getters:  
  - `PCIe_IsPCIeDevice()`  
  - `PCIe_GetLink()` → {Gen, Width, Negotiated}  
  - `PCIe_GetCaps()` → {MaxPayload, MRRS, Type}  
**Success:** `lspci` shows PCIe info; existing drivers unchanged.

---

## Step 6 — AER (Advanced Error Reporting, read-only)
**Goal:** debugging support.  
- Walk **extended capability chain** (offset 0x100 via ECAM).  
- Locate AER, dump status/log registers.  
**Success:** `pcictl aer-dump` prints AER info if present.

---

## Step 7 — PCIe Hotplug (optional)
**Goal:** react to slot insert/remove.  
- Use Slot Capabilities, Presence Detect, Attention Button.  
- Handle slot power state.  
**Success:** hotplug events logged cleanly if supported.

---

## Step 8 — SR-IOV / ARI (optional)
**Goal:** virtualization features.  
- SR-IOV: enumerate and enable virtual functions.  
- ARI: extended function addressing.  
**Success:** `pcictl sriov` lists VFs.

---

## Minimal Code Impact
- **Config Access:** add ECAM backend, switch automatically.  
- **Scan:** unchanged; enrich `PCI_DEVICE` with `PCIE_INFO`.  
- **Capabilities:** reuse `PCI_FindCapability`; add `PCI_FindExtendedCapability`.  
- **MSI/MSI-X:** new helpers, called on demand.  
- **BARs:** existing logic stays valid.

---

## Recommended Order & Criteria
1. ECAM → read registers >0xFF.  
2. PCIe Cap → link width/speed visible.  
3. MSI/MSI-X helpers → driver test IRQ.  
4. Payload/MRRS setters → performance tuning.  
5. PCIe getters → `lspci` enhanced.  
6. AER (optional).  
7. Hotplug (optional).  
8. SR-IOV/ARI (optional).  

---
