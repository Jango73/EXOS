# NVMe Implementation Plan for EXOS

## Prerequisites
- **PCIe enumeration** working (class 0x01, subclass 0x08 for NVMe).
- **MMIO mapping** (typically BAR0).
- **DMA buffers**: physically contiguous, 4 KiB aligned.
- **Interrupts**: MSI/MSI-X preferred (INTx fallback).
- **Logging**: concise traces (register reads/writes, SQ/CQ entries, status codes).

## Step 1 — Detect NVMe Controller
**Goal:** confirm NVMe presence and read capabilities.  
- Scan PCI config, find class 0x0108.  
- Map BAR0, read **CAP, VS, CC, CSTS, AQA, ASQ, ACQ**.  
- Record version and queue entry size limits.  
**Success:** `nvmectl info` prints controller version and max queue depth.

## Step 2 — Admin Queue Setup
**Goal:** enable the controller and exchange admin commands.  
- Allocate **ASQ**/**ACQ** in DMA memory and program **AQA/ASQ/ACQ**.  
- Set **CC.EN = 1**, wait for **CSTS.RDY = 1**.  
**Success:** `nvmectl ready` reports controller is up.

## Step 3 — Identify Controller & Namespace
**Goal:** retrieve identity and basic namespace info.  
- **Identify Controller** (Admin 0x06) → serial, model, firmware.  
- **Identify Namespace** (Admin 0x06, CNS=0x00, NSID=1).  
- Parse LBA formats (size, metadata).  
**Success:** `nvmectl list` shows NSID=1 with capacity in sectors.

## Step 4 — Create I/O Queues
**Goal:** operational I/O submission/completion queues.  
- **Create I/O CQ** (Admin 0x05), **Create I/O SQ** (Admin 0x01).  
- Route CQ to an MSI/MSI-X vector and arm interrupts.  
**Success:** dummy no-op commands complete on the I/O CQ.

## Step 5 — Read Sectors
**Goal:** implement the read path.  
- Build PRP (1–2 pages) for destination buffer.  
- Submit **Read (0x02)** to I/O SQ and wait for completion.  
**Success:** `dd if=/dev/nvme0n1 count=1` returns the MBR bytes.

## Step 6 — Write Sectors
**Goal:** implement the write path.  
- Build PRP for source buffer and submit **Write (0x01)**.  
- Optionally issue **Flush (0x00)**.  
**Success:** write a signature and verify by reading it back.

## Step 7 — Namespace Management
**Goal:** support multiple namespaces.  
- Enumerate all namespaces (**Identify CNS=0x02**).  
- Expose `/dev/nvme0n1`, `/dev/nvme0n2`, … nodes.  
**Success:** `nvmectl list` shows all namespaces with capacities.

## Step 8 — Error Handling & Reset
**Goal:** robust recovery.  
- Handle timeouts and wrap of SQ tail/CQ head.  
- On **CSTS.CFS = 1**: disable **CC.EN**, wait **RDY=0**, reinitialize.  
- Decode status codes (phase tag, SC, SCT), retry where appropriate.  
**Success:** bad commands or device hiccups do not panic the kernel.

## Step 9 — Performance & Multiqueue
**Goal:** scale with CPUs and load.  
- Per-CPU I/O SQ/CQ pairs.  
- Batch doorbells and support multiple outstanding commands.  
- One MSI-X vector per CQ when available.  
**Success:** sequential throughput increases with CPU count/queues.

## Step 10 — Features & Maintenance
**Goal:** useful admin features.  
- **Get/Set Features**: number of queues, write cache, arbitration.  
- **SMART/Health Log**: temperature, media errors, endurance stats.  
- **Dataset Management/TRIM (0x09)**.  
**Success:** `nvmectl smart` prints key health metrics.

## Step Decoupling (each step runs without the next)
- **1–3**: read-only discovery, safe to ship.  
- **4**: live queues, still no data movement required.  
- **5**: minimal read path, enough to mount filesystems read-only.  
- **6**: adds writes.  
- **7**: multi-namespace abstraction.  
- **8–9**: robustness and performance.  
- **10**: optional enhancements.

## QEMU Test Hints
- Single NVMe device:
```bash
qemu-system-x86_64 \
  -drive file=disk.img,if=none,id=nvme0 \
  -device nvme,drive=nvme0,serial=deadbeef
```
- Multiple namespaces: add additional `nvme-ns` devices bound to the same controller.
