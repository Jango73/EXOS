# Predator xHCI Debug Handover

## Context
- Platform: Predator physical machine (split console view only).
- Main issue reported: repeated xHCI timeouts and no USB key visible in filesystem list after boot.

## Initial Symptoms
- Repeating failures on the same root ports: `1`, `4`, `5`, `14`.
- Frequent messages:
  - `XHCI_WaitForCommandCompletion Timeout 200 ms`
  - `XHCI_ProbePort Port X enumerate failed`
- `usb devices` initially showed no USB device in some runs.
- `fs` showed only internal/NVMe partitions.

## Confirmed Facts From Diagnostics
- BIOS/UEFI boot from USB works, but kernel xHCI path fails later (firmware driver vs kernel driver handoff).
- At failure time, controller state was observed as:
  - `USBCMD=0x0`
  - `USBSTS=0x1d` (`Halted=1`, `HSE=1`)
- PCI status showed bus error bits (`MA=1` observed in decoded status).
- `CRCR` looked invalid during fault episodes (`...:0x8`), indicating command processing breakdown after fault.
- The same ports recurring does not imply random noise; they are stable hardware topology entries.

## Major Changes Implemented

### 1) Console split clear behavior
- `ClearConsole()` changed to clear only region 0 when debug split is enabled.
- Effect: `data`/System Data View no longer wipes the log pane in split mode.
- File: `kernel/source/Console.c`

### 2) xHCI event ring EHB handling
- ERDP update now sets EHB bit during event dequeue.
- Files:
  - `kernel/include/drivers/XHCI-Internal.h`
  - `kernel/source/drivers/XHCI-Core.c`

### 3) Focused xHCI logging and filter-driven diagnostics
- Added rate-limited probe failure log:
  - tag: `[XHCI_LogProbeFailure]`
- Added rate-limited enable-slot timeout state snapshot:
  - tag: `[XHCI_LogEnableSlotTimeoutState]`
- Added init register programming/readback log:
  - tag: `[XHCI_LogInitReadback]`
- Added one-shot first HSE transition log:
  - tag: `[XHCI_LogHseTransition]`
- Files:
  - `kernel/source/drivers/XHCI-Device.c`
  - `kernel/source/drivers/XHCI-Core.c`
  - `kernel/include/drivers/XHCI-Internal.h`

### 4) System Data View xHCI page expansion
- Added deep xHCI page fields:
  - PCI identity, MMIO bases, runtime register sets, ring indexes, queue depth
  - `USBCMD/USBSTS/CONFIG`, `CRCR`, `DCBAAP`, `ERSTBA`, `ERDP`
  - decoded PCI status flags
  - per-port enum result + mass-storage hint (`MS-BOT`, `MS-UAS`, etc.)
  - USB mass storage driver status and USB storage entry counters
- File: `kernel/source/SystemDataView.c`

### 5) Scratchpad support (critical)
- Added scratchpad count extraction from `HCSPARAMS2`.
- Allocates scratchpad pages and scratchpad pointer array.
- Programs `DCBAA[0]` correctly.
- Frees scratchpad resources on teardown.
- Files:
  - `kernel/include/drivers/XHCI-Internal.h`
  - `kernel/source/drivers/XHCI-Core.c`

### 6) Driver typing cleanup
- Added `DRIVER_TYPE_XHCI` and assigned xHCI driver to it.
- Files:
  - `kernel/include/Driver.h`
  - `kernel/source/drivers/XHCI-Core.c`

### 7) USB mass storage diagnostics + deferred mount retry
- Added scan logs for storage attach decisions:
  - tag: `[USBMassStorageScan]`
- Added deferred partition mount retry path (`MountPending`) in poll loop.
- File: `kernel/source/drivers/USBMassStorage.c`

### 8) Current targeted fix for attach failure
- Latest observed blocker:
  - `[USBMassStorageStartDevice] Bulk OUT endpoint setup failed`
- Implemented idempotent handling in `XHCI_AddBulkEndpoint()`:
  - if endpoint already configured, return success
  - if configure command fails but endpoint is already configured, accept success
- File: `kernel/source/drivers/XHCI-Device.c`

## Key Observations From Latest Predator Capture
- `USBMassStorage Driver Ready=1`
- Port 1 reports `MS=MS-BOT` (device is recognized as BOT-capable mass storage).
- Still failing at:
  - `[USBMassStorageStartDevice] Bulk OUT endpoint setup failed`
- Port 14 continues to show enumeration failure but is not the primary blocker for USB key on port 1.

## Additional Observations From New Predator Capture (after latest xHCI fixes)
- Bulk endpoint setup still fails on port 1 with the new tags enabled:
  - `[XHCI_ConfigureEndpoint] Timeout Slot=0x1 USBCMD=0x1 USBSTS=0x18`
  - `[XHCI_AddBulkEndpoint] Configure failed Slot=0x1 DCI=0x4 EP=0x2 MPS=512`
  - `[USBMassStorageStartDevice] Bulk OUT endpoint setup failed`
- The timeout-state snapshot is now emitted during this failure path:
  - `[XHCI_LogEnableSlotTimeoutState] ... USBCMD=0x1 USBSTS=0x18 ... PCICMD=0x6 PCISTS=0x290 ...`
- Parallel noise on port 14 is still present and now reaches enable-slot timeout class:
  - `[XHCI_LogProbeFailure] Port 14 step=EnableSlot err=0x5 completion=0xfff raw=0x220603 ...`
  - `[XHCI_LogProbeFailure] Port 14 step=EnumerateDevice err=0x5 completion=0xfff raw=0x220603 ...`
- Practical conclusion from this capture:
  - The controller is not in the same halted/HSE state as earlier captures (`USBSTS=0x18` here), but the `Configure Endpoint` command for BOT OUT endpoint (`DCI=0x4`, `EP=0x2`) still does not complete on Predator.

## Logging Filter Notes
- User requirement: set default log filter in `scripts/build.sh` (not in `kernel/Makefile`).
- Final preference during session:
  - keep `kernel/Makefile` neutral defaults
  - put xHCI/USB storage debug tags in `scripts/build.sh` default filter.

## Open Problem (At Handover)
- USB key is detected at xHCI level (`MS-BOT`) but fails during BOT attach due to bulk OUT endpoint setup path.
- Filesystem list still does not expose USB disk partitions.

## Suggested Next Steps
1. Instrument `XHCI_AddBulkEndpoint()` to log completion code from configure endpoint path specifically for BOT endpoints.
2. Verify DCI/state for both BOT endpoints (OUT then IN) against current device context before and after configure.
3. If required, switch BOT setup to configure both bulk endpoints in one configure transaction for the same input context.
4. Keep log filtering constrained to xHCI + USBMassStorage tags for Predator screen readability.
