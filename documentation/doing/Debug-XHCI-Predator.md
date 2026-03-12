# Predator xHCI Debug

## Scope
- Target: Predator bare metal machine.
- Problem: USB mass storage enumeration reaches BOT, then fails on bulk IN data stage, so USB key does not become usable in `fs`.
- Constraint: no config file loaded from USB during failure path; behavior uses internal default values.

## Confirmed From Predator Screenshots
- xHCI controller is detected and ports are visible.
- `usb devices` shows:
  - `Port 1 Addr 1 VID=0x5e3 PID=0x736 Speed=HS`
  - `Port 4 Addr 2 VID=0x1017 PID=0x900a Speed=FS`
  - `Port 5 Addr 3 VID=0x408 PID=0xa060 Speed=HS`
- `fs` shows internal entries and one 1 GB NTFS partition (`n0p3*`), but no mounted USB letter (`u`) is available.
- Port 14 remains noisy with repeated probe backoff logs.

## BOT Failure Sequence (Observed)
- `USBStorageStartDevice` starts on `Port=1 Addr=1 Slot=0x1`, class `0x08/0x06/0x50`, endpoints:
  - Bulk OUT `0x02`, Attr `0x02`, MPS `512`
  - Bulk IN `0x81`, Attr `0x02`, MPS `512`
- `USBStorageBotCommand`:
  - `Op=0x12` (INQUIRY) is issued.
  - `Op=0x25` (READ CAPACITY 10) is issued.
- Failure occurs on READ CAPACITY data stage:
  - repeated `USBStorageBulkTransferOnce Timeout ... Ep=0x81 DirIn=1 Len=8`
  - retries `Attempt=2/3`, `Attempt=3/3`
  - `USBStorageBotCommand Data stage failed Op=0x25`
  - `USBStorageStartDevice READ CAPACITY failed, attempting reset`
- Recovery path also fails on IN data:
  - `USBStorageBotCommand Op=0x03` (REQUEST SENSE), `DataLen=18`, then bulk IN timeouts on `Ep=0x81`.

## Implemented Changes (Code)

### xHCI transfer direction handling
- Removed `XHCI_TRB_DIR_IN` from `TRB_TYPE_NORMAL` submissions (direction must come from endpoint context for normal TRBs):
  - `kernel/source/drivers/storage/USBStorage-Transport.c`
  - `kernel/source/drivers/usb/XHCI-Hub.c`
  - `kernel/source/drivers/input/Keyboard-USB.c`
  - `kernel/source/drivers/input/Mouse-USB.c`
- `XHCI_TRB_DIR_IN` remains used for control transfer data/status TRBs where required by transfer type.

### xHCI wait loops
- Replaced xHCI polling loops based on arbitrary iteration counters by millisecond loops using `Sleep(1)`:
  - `XHCI_WaitForRegister`
  - `XHCI_WaitForCommandCompletion`
  - `XHCI_WaitForTransferCompletion`

### Early boot delay behavior
- Added fallback busy-wait path before first interrupt enable:
  - `BusyWaitMilliseconds` + CPU-frequency-based calibration.
  - `Sleep` uses busy wait while system time is not operational.
- This avoids dependence on `GetSystemTime` before the first `EnableInterrupts`.

### USB storage robustness fix
- Fixed READ(10) command block builder to take explicit buffer length and validate it:
  - introduced `USB_SCSI_READ_10_COMMAND_BLOCK_LENGTH`.
  - `USBStorageBuildRead10` checks `CommandBlockLength >= 10`.
  - files:
    - `kernel/include/drivers/storage/USBStorage-Private.h`
    - `kernel/source/drivers/storage/USBStorage-Transport.c`

### xHCI transfer completion matching fallback
- Added completion fallback for BOT transfers using `(SlotId, EndpointId)` when transfer-event TRB pointer does not match the enqueued TRB pointer:
  - logs expected vs observed event TRB pointer.
  - keeps pointer-based matching as first path.
  - files:
    - `kernel/source/drivers/usb/XHCI-Core.c`
    - `kernel/source/drivers/usb/XHCI-Hub.c`
    - `kernel/source/drivers/storage/USBStorage-Transport.c`

### xHCI completion queue concurrency fix
- Fixed concurrent completion queue/event ring access between wait paths and interrupt bottom-half:
  - `XHCI_InterruptBottomHalf` now takes `Device->Mutex` while polling/compressing event completions.
  - this aligns interrupt-side completion ingestion with command/transfer wait paths already using the same mutex.
  - file:
    - `kernel/source/drivers/usb/XHCI-Core.c`

### BOT transfer recovery hardening (xHCI side)
- Added xHCI endpoint recovery for BOT bulk endpoints on timeout/stall:
  - choose `STOP_ENDPOINT`, `RESET_ENDPOINT`, or direct dequeue reprogramming based on current endpoint state.
  - reset software transfer ring state before retry.
  - use correctly encoded `SET_TR_DEQUEUE_POINTER`.
  - keep USB `CLEAR_FEATURE(ENDPOINT_HALT)` as protocol-level recovery step.
  - files:
    - `kernel/source/drivers/usb/XHCI-Device-Lifecycle.c`
    - `kernel/source/drivers/storage/USBStorage-Transport.c`

### BOT specification compliance fixes
- Fixed BOT command status handling to follow the Bulk-Only Transport rules:
  - preserve the expected CBW tag before the shared IO buffer is reused for the CSW.
  - validate CSW signature and tag against the original CBW.
  - validate `DataResidue <= dCBWDataTransferLength`.
  - treat `bCSWStatus == 0x02` as phase error and force reset recovery.
  - force reset recovery on invalid CSW, invalid residue, and transport-stage failures.
- Added transfer-event residual length propagation from xHCI to BOT:
  - xHCI completion queue stores transfer-event transfer length.
  - BOT derives actual CSW receive length from requested length minus residual length.
  - short or oversized CSW transfers are rejected.
- files:
  - `kernel/include/drivers/storage/USBStorage-Private.h`
  - `kernel/source/drivers/storage/USBStorage-Transport.c`
  - `kernel/include/drivers/usb/XHCI-Internal.h`
  - `kernel/source/drivers/usb/XHCI-Core.c`
  - `kernel/source/drivers/usb/XHCI-Hub.c`

## External Driver Cross-Check
- Linux xHCI behavior was used as reference for normal TRB direction semantics.
- No Predator-specific mandatory quirk for `Intel 8086:a36d` was identified from that check for this exact symptom.
- Working hypothesis remains implementation gap in EXOS xHCI/BOT path, not a proven required hardware quirk.

## Status
- Bulk OUT path reaches device.
- `SET_TR_DEQUEUE_POINTER` no longer fails with `Completion=0x13`; xHCI endpoint recovery completes with `SetCompletion=0x1`.
- BOT CSW handling is now strict and spec-aligned.
- Remaining blocker on Predator is still Bulk IN on `Ep=0x81`:
  - `READ CAPACITY (10)` data stage fails.
  - `REQUEST SENSE` data stage can timeout.
  - CSW read can also timeout.
- xHCI stack is partially functional (device visible, BOT init started, mouse interrupt report submitted) but storage attach remains blocked on bulk IN transfer completion.

## Next Debug Focus
- Audit xHCI Bulk IN transfer semantics for `Ep=0x81` / `Dci=3`:
  - transfer-event transfer length interpretation.
  - short-packet handling on normal TRBs.
  - dequeue/ring progression after recovery.
- Verify endpoint context fields for DCI of bulk IN after `Configure Endpoint`:
  - endpoint state, dequeue pointer, MPS, endpoint type.
- Check whether the mouse interrupt IN endpoint ever reaches completion on Predator:
  - device discovery and report submission are confirmed.
  - report completion is not yet proven.
- Keep log filter focused on xHCI + USB storage + USB mouse tags to preserve screenshot readability.

## xHCI Specification Compliance Audit

### Specification reference used
- Intel xHCI Requirements Specification for USB, Revision 1.2 (May 2019):
  - https://www.intel.cn/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf

### Findings and status
- Setup Stage TRB `TRT` field not set before fix:
  - Spec reference: section 6.4.1.2 (Setup Stage TRB), `TRT` bits `[17:16]` and Table 4-7 control transfer stage flow.
  - Requirement: `TRT` must encode `No Data`, `OUT Data`, or `IN Data`.
  - Previous code left `TRT=0` for all requests.
  - Status: fixed in `XHCI_ControlTransfer`.

- Endpoint Context `CErr` encoding not compliant before fix:
  - Spec reference: section 6.2.3 (Endpoint Context, DW1 field layout) and section 4.8.2 (context initialization values).
  - Requirement: `CErr` uses bits `[2:1]`, bit `0` is reserved, and non-isochronous endpoints should use `CErr=3`.
  - Previous code encoded `3` in low bits directly, setting a reserved bit and not placing `CErr` correctly.
  - Status: fixed for EP0, bulk, and interrupt endpoint context builders.

- `USBCMD.INTE` not managed before fix:
  - Spec reference: section 5.4.1 (`USBCMD` register, `INTE`) and section 4.2 initialization sequence.
  - Requirement: host controller interrupt enable uses both interrupter state and `USBCMD.INTE`.
  - Previous code configured `IMAN.IE` but did not set/clear `USBCMD.INTE`.
  - Status: fixed in `XHCI_SetInterruptEnabled`.

- SuperSpeed endpoint companion parameters:
  - Spec reference: section 4.8.2.3 / 4.8.2.4 (`bMaxBurst`, `Mult`, streams fields).
  - Requirement: SS endpoint context parameters must be derived from SS companion descriptors.
  - Parsing/storage for SS endpoint companion descriptors is implemented and `bMaxBurst` + interrupt `Mult` are propagated to endpoint contexts.
  - Status: partially fixed (stream-context programming for bulk streams remains open).

- Interrupt endpoint context fields were still not fully spec-compliant:
  - Spec reference: section 4.8.2.3 / 4.8.2.4 (interrupt endpoint context), USB 2.0 endpoint descriptor `wMaxPacketSize` transaction encoding, and xHCI endpoint context `Interval` / `Max Burst Size` / `Max ESIT Payload`.
  - Requirement:
    - high-speed interrupt endpoints derive additional transactions from `wMaxPacketSize[12:11]`;
    - full-speed and low-speed interrupt endpoints require interval conversion from frames to the xHCI exponent encoding;
    - interrupt `Mult` stays zero;
    - periodic endpoint context must program `Max ESIT Payload`.
  - Previous behavior:
    - used raw `bInterval` except for a simple HS/SS decrement;
    - left `Max ESIT Payload` at zero;
    - propagated SS companion `Mult` into interrupt endpoints.
  - Status: fixed in `XHCI_AddInterruptEndpoint`.

- Cached transfer completions from a previous timeout/recovery cycle could survive long enough to be matched by route fallback on the same `Slot/DCI`:
  - Evidence: historical Predator logs showed `Transfer event TRB pointer mismatch ... Expected=0x... Observed=0x... Completion=0x1` after endpoint reset on `Dci=3`.
  - Requirement: a new transfer must not consume a stale transfer event that belongs to the previous ring state.
  - Status: mitigated by purging cached transfer completions for the route before re-arming a fresh normal transfer and immediately after endpoint dequeue reprogramming.

- Endpoint recovery sequence was not xHCI-compliant before fix:
  - Spec reference: section 4.6.9 (Reset Endpoint Command) and section 4.6.10 (Set TR Dequeue Pointer Command).
  - Requirement:
    - `RESET_ENDPOINT` is valid for halted endpoint recovery (stall path).
    - non-halted timeout recovery must stop endpoint and reprogram dequeue pointer; software-only ring reset is insufficient.
  - Previous behavior:
    - always issued `STOP_ENDPOINT` + `RESET_ENDPOINT`, including timeout path.
    - reset software ring state without `SET_TR_DEQUEUE_POINTER`.
    - this can produce context-state command errors (`Completion=0xb`) and leave controller dequeue unsynchronized.
  - Status: fixed:
    - timeout path: `STOP_ENDPOINT` + software ring reset + `SET_TR_DEQUEUE_POINTER`.
    - stall path: `RESET_ENDPOINT` + software ring reset + `SET_TR_DEQUEUE_POINTER`.

### Last run log

```
T14900> WARNING > [XHCI_ProbePortBackoff] Port=14 Step=Cooldown Failures=1 Applied=0 Remaining=15100 suppressed=2
T14900> DEBUG > [USBStorageStartDevice] Begin Port=1 Addr=1 Slot=0x1 If=0 Class=0x8/0x6/0x50 Vid=0x5e3 Pid=0x736 BulkOut=0x2 Attr=0x2 MPS=512 BulkIn=0x81 Attr=0x2 MPS=512
T14940> DEBUG > [USBStorageBotCommand] Op=0x12 CbLen=6 DataLen=36 DirIn=1 Tag=0x1 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
T15020> DEBUG > [USBStorageBotCommand] Op=0x25 CbLen=10 DataLen=8 DirIn=1 Tag=0x2 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
T15410> WARNING > [USBStorageBulkTransfer] xHCI endpoint reset failed after stall Slot=0x1 Dci=3 Ep=0x81
T17810> WARNING > [USBStorageBulkTransferOnce] Timeout Slot=0x1 Port=1 Addr=1 Ep=0x81 Dci=3 DirIn=1 Len=8 Trb=0000000000618000
T23840> WARNING > [USBStorageBulkTransfer] Attempt=2/3 failed Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1
T23920> WARNING > [USBStorageBulkTransfer] xHCI endpoint reset failed Slot=0x1 Dci=3 Ep=0x81
T23960> WARNING > [USBStorageWaitCompletion] Transfer event TRB pointer mismatch Slot=0x1 Dci=3 Expected=0x0:0x618000 Observed=0x0:0x618030 Completion=0x1
T23960> WARNING > [USBStorageBulkTransfer] Completion=0x1 Attempt=3/3 Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1 Len=8
T23960> ERROR > [USBStorageBotCommand] Data stage failed Op=0x25 Tag=0x2 DirIn=1 Len=8
T23960> WARNING > [USBStorageStartDevice] READ CAPACITY failed, attempting reset
T23960> DEBUG > [USBStorageBotCommand] Op=0x3 CbLen=6 DataLen=18 DirIn=1 Tag=0x3 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
T32380> WARNING > [USBStorageBulkTransferOnce] Timeout Slot=0x1 Port=1 Addr=1 Ep=0x81 Dci=3 DirIn=1 Len=18 Trb=0000000000618020
T32380> WARNING > [USBStorageBulkTransfer] Attempt=1/3 failed Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1
T32500> WARNING > [USBStorageBulkTransfer] xHCI endpoint reset failed Slot=0x1 Dci=3 Ep=0x81
T32500> WARNING > [USBStorageWaitCompletion] Transfer event TRB pointer mismatch Slot=0x1 Dci=3 Expected=0x0:0x618000 Observed=0x0:0x618030 Completion=0x1
T32500> WARNING > [USBStorageBulkTransfer] Completion=0x1 Attempt=2/3 Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1 Len=18
T32500> ERROR > [USBStorageBotCommand] Data stage failed Op=0x3 Tag=0x3 DirIn=1 Len=18
T32600> WARNING > [USBStorageRequestSense] REQUEST SENSE failed LastOp=0x3 Stage=4 LastCSW=0xff Residue=0
T32620> DEBUG > [USBStorageBotCommand] Op=0x25 CbLen=10 DataLen=8 DirIn=1 Tag=0x4 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
T41020> WARNING > [USBStorageBulkTransferOnce] Timeout Slot=0x1 Port=1 Addr=1 Ep=0x81 Dci=3 DirIn=1 Len=8 Trb=0000000000618010
T41020> WARNING > [USBStorageBulkTransfer] Attempt=1/3 failed Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1
T41140> WARNING > [USBStorageBulkTransfer] xHCI endpoint reset failed Slot=0x1 Dci=3 Ep=0x81
T41140> WARNING > [USBStorageWaitCompletion] Transfer event TRB pointer mismatch Slot=0x1 Dci=3 Expected=0x0:0x618000 Observed=0x0:0x618030 Completion=0x1
T41140> WARNING > [USBStorageBulkTransfer] Completion=0x1 Attempt=2/3 Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1 Len=8
T41140> ERROR > [USBStorageBotCommand] Data stage failed Op=0x25 Tag=0x4 DirIn=1 Len=8
T41140> DEBUG > [USBStorageBotCommand] Op=0x3 CbLen=6 DataLen=18 DirIn=1 Tag=0x5 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
T49620> WARNING > [USBStorageBulkTransferOnce] Timeout Slot=0x1 Port=1 Addr=1 Ep=0x81 Dci=3 DirIn=1 Len=18 Trb=0000000000618010
T49620> WARNING > [USBStorageBulkTransfer] Attempt=1/3 failed Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1
T49680> WARNING > [USBStorageBulkTransfer] xHCI endpoint reset failed Slot=0x1 Dci=3 Ep=0x81
T49620> WARNING > [USBStorageWaitCompletion] Transfer event TRB pointer mismatch Slot=0x1 Dci=3 Expected=0x0:0x618000 Observed=0x0:0x618030 Completion=0x1
T49620> WARNING > [USBStorageBulkTransfer] Completion=0x1 Attempt=2/3 Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1 Len=18
T49620> ERROR > [USBStorageBotCommand] Data stage failed Op=0x3 Tag=0x5 DirIn=1 Len=18
T49620> WARNING > [USBStorageRequestSense] REQUEST SENSE failed LastOp=0x3 Stage=4 LastCSW=0xff Residue=0
T49620>
T49620> WARNING > [USBStorageScan] Port=1 Addr=1 If=0 Class=0x8/0x6/0x50 Reason=StartDeviceFailed
T52780> WARNING > [XHCI_LogProbeFailure] Port=14 Step=AddressDevice Err=0x6 Completion=0x0 Raw=0x220603 CCS=1 PED=1 PR=0 PLS=0 Speed=0x1 Slot=0x5 Addr=0x0 Present=0 Hub=0 Uid=0x0 Pid=0x0 Class=0x0/0x0/0x0 MPS0=8 USBCMD=0x1 USBSTS=0x18 suppressed=0
T52780> WARNING > [XHCI_LogProbeFailure] Port=14 Step=EnumerateDevice Err=0x0 Completion=0x0 Raw=0x220603 CCS=1 PED=1 PR=0 PLS=0 Speed=0x1 Slot=0x5 Addr=0x0 Present=0 Hub=0 Uid=0x0 Pid=0x0 Class=0x0/0x0/0x0 MPS0=8 USBCMD=0x1 USBSTS=0x18 suppressed=0
T52780> DEBUG > [USBStorageStartDevice] Begin Port=1 Addr=1 Slot=0x1 If=0 Class=0x8/0x6/0x50 Vid=0x5e3 Pid=0x736 BulkOut=0x2 Attr=0x2 MPS=512 BulkIn=0x81 Attr=0x2 MPS=512
T52900> DEBUG > [USBStorageBotCommand] Op=0x12 CbLen=6 DataLen=36 DirIn=1 Tag=0x1 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
```

## Latest Predator Log

Date: 2026-03-12 (local debug run after commits through `4b255051`)

### What the log sequence confirms
- Mouse path:
  - USB mouse is detected and started on `Addr=0x2`, `Ep=0x81`, `Dci=0x3`.
  - report submission succeeds (`USBMouseSubmitReport Submitted report len=8 ... dci=0x3`).
  - report completion is not yet proven by the captured logs.
- BOT initialization:
  - `INQUIRY (Op=0x12)` succeeds.
  - `READ CAPACITY (10)` still fails on the Bulk IN data stage (`Len=8`, `Ep=0x81`, `Dci=3`).
- Recovery path:
  - `XHCI_ResetTransferEndpoint` completes with `SetCompletion=0x1`.
  - the earlier `SET_TR_DEQUEUE_POINTER Completion=0x13` failure is no longer present.
- New correction after this run:
  - interrupt endpoint context encoding was tightened for non-SuperSpeed periodic devices;
  - cached transfer completions are purged on fresh single-TRB submissions and after dequeue reprogramming.
- Follow-up BOT failures:
  - `REQUEST SENSE (Op=0x03)` data stage can timeout on Bulk IN (`Len=18`).
  - CSW read can also timeout (`Len=13`), producing `USBStorageBotCommand CSW read failed`.

## Latest Predator Log Follow-up

Date: 2026-03-12 (local debug run after interrupt-context and stale-completion fixes)

### What changed
- Transfer-event fallback mismatch logs are no longer visible in the captured output.
- `XHCI_ResetTransferEndpoint` still succeeds with `SetCompletion=0x1`.
- Endpoint recovery continues to end in `FinalState=0x3`.

### What did not change
- BOT `READ CAPACITY (10)` still fails on the Bulk IN data stage (`Len=8`, `Ep=0x81`, `Dci=3`).
- BOT `REQUEST SENSE (0x03)` still times out on the Bulk IN data stage (`Len=18`).
- CSW read still times out on Bulk IN (`Len=13`).
- USB mouse report submission is still visible, but report completion is still not proven.

### Interpretation
- Purging cached transfer completions appears to have removed one false-positive path: no more stale `Expected/Observed` transfer-event mismatch is visible.
- The remaining blocker is still a real IN-transfer completion problem on xHCI endpoint `Dci=3`.
- Because the same endpoint direction is involved for both BOT `Bulk IN` and the USB mouse interrupt report path, the shared suspect remains the generic xHCI IN-transfer path rather than BOT parsing.

### Next audit focus
- Audit `Normal TRB` IN submission semantics against xHCI spec:
  - transfer length field usage,
  - interrupt-on-short-packet behavior,
  - constraints on single-TRB TD usage.
- Audit `Transfer Event` expectations for single-TRB IN transfers:
  - when a controller may legally withhold completion versus report short packet / success.
- Audit common IN-endpoint programming shared by bulk and interrupt paths:
  - dequeue pointer validity after recovery,
  - endpoint context fields that still differ between QEMU tolerance and Predator behavior.

## Additional Hostile Review Result

Date: 2026-03-12

- Doorbell ordering was still relying on implicit CPU/MMIO ordering:
  - QEMU often hides this class of issue because guest memory updates are observed synchronously by the emulated controller.
  - Real hardware is less forgiving if ring/context writes are not published before the doorbell MMIO write.
  - Status: fixed by publishing ring/context memory with an explicit barrier in `XHCI_RingDoorbell`.

- After this pass, no remaining field-level xHCI spec violation stands out in the minimal single-TRB IN path as clearly as the earlier fixes (`SET_TR_DEQUEUE_POINTER`, endpoint-state recovery, BOT CSW validation, interrupt context encoding).
  - That does not prove the path is correct.
  - It does mean the remaining bug is increasingly likely to be in a smaller hardware-facing contract: transfer publication ordering, a still-misencoded context detail not yet surfaced by logs, or a controller-specific assumption that QEMU tolerates.

## Predator Result After Doorbell Barrier

Date: 2026-03-12

- Predator behavior did not change after adding the doorbell publication barrier.
- Conclusion:
  - the barrier was a plausible hardware-facing fix, but it did not address the active failure on this machine.
  - the debug focus narrows again to the BOT `Bulk IN` path itself, using the minimal sequence:
    - configure Bulk IN endpoint,
    - submit one `Normal TRB` IN,
    - wait for one transfer event,
    - recover endpoint only after a real timeout or halt.

## Current Debug Strategy

- Ignore the USB mouse path for the next pass.
- Ignore generic “shared Dci=3” theorizing unless the BOT `Bulk IN` audit proves a common xHCI fault.
- Audit only BOT `Bulk IN` on Predator:
  - `READ CAPACITY (10)` data stage,
  - `REQUEST SENSE` data stage,
  - CSW IN stage.
- Treat every remaining change as suspect until justified directly against the BOT + xHCI spec path for a single Bulk IN transfer.

## BOT Bulk IN Audit

Date: 2026-03-12

### Objective finding
- The BOT `Bulk IN` error path was not following the BOT transport state machine.
- Previous behavior in `USBStorageBulkTransfer`:
  - on `STALL`, it reset the xHCI endpoint, cleared the halted USB endpoint, and retried the same data transfer;
  - on timeout, it reset the xHCI endpoint, cleared the halted USB endpoint, and retried the same data transfer.
- This mixes xHCI endpoint recovery with BOT protocol policy and replays the data stage in cases where BOT expects a different next step.

### Relevant BOT requirement
- BOT status-transport flow:
  - if the data transport or CSW transport stalls, the host clears the halted pipe and proceeds to CSW handling;
  - transport errors that are not this stall-handling path require BOT reset recovery.
- In particular, replaying the same Bulk IN data stage after a stall is not the BOT-prescribed next step.

### Status
- Corrected in `USBStorage-Transport.c`:
  - timeout is treated as transfer failure and falls back to BOT reset recovery at command level;
  - data-stage `STALL` clears the affected bulk pipe and proceeds to CSW handling;
  - CSW `STALL` clears Bulk IN and retries CSW once instead of replaying the earlier data stage.

### Practical interpretation
- The previously confirmed xHCI dequeue-command encoding bug is fixed.
- The previously confirmed BOT CSW validation bugs are fixed.
- The remaining blocker is a real transfer problem on Bulk IN, not a false BOT parse/validation failure.
- Failure scope remains concentrated on `Ep=0x81` / `Dci=3`.

## Expected vs Observed Chain

Expected (READ CAPACITY, Bulk IN):

`USBStorageBotCommand(Op=0x25)`  
`-> USBStorageBulkTransfer(DATA IN, Ep=0x81, Dci=3, Len=8)`  
`-> USBStorageWaitCompletion(Completed before timeout)`  
`-> (if recovery needed) XHCI_ResetTransferEndpoint(Begin)`  
`-> XHCI_StopEndpoint(Completion=success)`  
`-> XHCI_SetTransferRingDequeuePointer(Completion=success)`  
`-> XHCI_ResetTransferEndpoint(End Success=1)`  
`-> retry proceeds with synchronized endpoint context/dequeue`

Observed (latest Predator screenshot, right pane):

`USBStorageBotCommand(Op=0x25)`  
`-> USBStorageBulkTransfer(DATA IN, Ep=0x81, Dci=3, Len=8)`  
`-> XHCI_ResetTransferEndpoint(Begin ... CtxState=0x2)`  
`-> XHCI_ResetTransferEndpoint(End ... FinalState=0x3 SetCompletion=0x1)`  
`-> USBStorageBotCommand Data stage failed Op=0x25`  
`-> REQUEST SENSE data stage timeout (Len=18)`  
`-> REQUEST SENSE CSW read timeout (Len=13)`  
`-> cascade to REQUEST SENSE failure path`

## Last Log

```
T160> DEBUG > [XHCI_LogProbeFailure] Port=14 Step=AddressDevice Err=0x6 Completion=0x0 Raw=0x220603 ...
T160> DEBUG > [XHCI_LogProbeFailure] Port=14 Step=EnumerateDevice Err=0x6 Completion=0x0 Raw=0x220603 ...
T6620> DEBUG > [XHCI_WaitForTransferCompletion] Timeout 200 ms (TRB=...)
T6740> DEBUG > [XHCI_LogProbeFailure] Port=14 Step=AddressDevice Err=0x6 Completion=0x0 Raw=0x220603 ...
T6740> DEBUG > [XHCI_LogProbeFailure] Port=14 Step=EnumerateDevice Err=0x6 Completion=0x0 Raw=0x220603 ...
T6740> DEBUG > [USBStorageStartDevice] Begin Port=1 Addr=1 Slot=0x1 If=0 Class=0x8/0x6/0x50 ...
T6780> DEBUG > [USBStorageBotCommand] Op=0x12 CdbLen=6 DataLen=36 DirIn=1 Tag=0x1 ...
T6780> DEBUG > [USBStorageWaitCompletion] Begin Timeout=1000 Trb=0x0:0x617000 suppressed=0
T6780> DEBUG > [USBStorageWaitCompletion] Completed Elapsed=0 Completion=0x1 Trb=0x0:0x617000 suppressed=0
T16620> DEBUG > [USBStorageStartDevice] Begin Port=1 Addr=1 Slot=0x1 If=0 Class=0x8/0x6/0x50 Vid=0x5e3 Pid=0x736 BulkOut=0x2 Attr=0x2 MPS=512 BulkIn=0x81 Attr=0x2 MPS=512
T16780> DEBUG > [USBStorageBotCommand] Op=0x12 CdbLen=6 DataLen=36 DirIn=1 Tag=0x1 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
T16780> DEBUG > [USBStorageWaitCompletion] Begin Timeout=1000 Trb=0x0:0x525000 suppressed=0
T16780> DEBUG > [USBStorageWaitCompletion] Completed Elapsed=0 Completion=0x1 Trb=0x0:0x525000 suppressed=0
T16860> DEBUG > [USBStorageBotCommand] Op=0x25 CdbLen=10 DataLen=8 DirIn=1 Tag=0x2 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
T17160> DEBUG > [XHCI_ResetTransferEndpoint] Begin Slot=0x1 DCI=0x3 Halted=1 CtxState=0x2
T17240> DEBUG > [XHCI_ResetTransferEndpoint] End Slot=0x1 DCI=0x3 Halted=1 InitialState=0x2 FinalState=0x3 StopCompletion=0x0 SetCompletion=0x1 Dequeue=0x0:0x61e001
T17440> ERROR > [USBStorageBotCommand] Data stage failed Op=0x25 Tag=0x2 DirIn=1 Len=8
T17440> WARNING > [USBStorageStartDevice] READ CAPACITY failed, attempting reset
T17440> DEBUG > [USBStorageBotCommand] Op=0x3 CdbLen=6 DataLen=18 DirIn=1 Tag=0x3 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
T149260> DEBUG > [USBStorageWaitCompletion] Timeout Elapsed=1000 Trb=0x0:0x61e010 suppressed=17
T149260> DEBUG > [USBStorageBulkTransferOnce] Timeout Op=0x3 Slot=0x1 Port=1 Addr=1 Ep=0x81 Dci=3 DirIn=1 Len=18 Trb=...
T149300> DEBUG > [XHCI_ResetTransferEndpoint] Begin Slot=0x1 DCI=0x3 Halted=0 CtxState=0x2
T149340> DEBUG > [XHCI_ResetTransferEndpoint] End Slot=0x1 DCI=0x3 Halted=0 InitialState=0x2 FinalState=0x3 StopCompletion=0x0 SetCompletion=0x1 Dequeue=0x0:0x61e001
T157700> DEBUG > [USBStorageWaitCompletion] Begin Timeout=1000 Trb=0x0:0x61e000 suppressed=1
T199460> DEBUG > [USBStorageWaitCompletion] Timeout Elapsed=1000 Trb=0x0:0x61e010 suppressed=2
T199460> DEBUG > [USBStorageBulkTransferOnce] Timeout Op=0x3 Slot=0x1 Port=1 Addr=1 Ep=0x81 Dci=3 DirIn=1 Len=13 Trb=...
T199460> DEBUG > [XHCI_ResetTransferEndpoint] Begin Slot=0x1 DCI=0x3 Halted=0 CtxState=0x1
T199540> DEBUG > [XHCI_ResetTransferEndpoint] End Slot=0x1 DCI=0x3 Halted=0 InitialState=0x1 FinalState=0x3 StopCompletion=0x1 SetCompletion=0x1 Dequeue=0x0:0x61e001
T199700> ERROR > [USBStorageBotCommand] CSW read failed Op=0x3 Tag=0x0
T199700> WARNING > [USBStorageRequestSense] REQUEST SENSE failed LastOp=0x3 Stage=6 LastCSW=0xff Residue=0
T199820> DEBUG > [USBStorageBotCommand] Op=0x25 CdbLen=10 DataLen=8 DirIn=1 Tag=0x4 Slot=0x1 Port=1 Addr=1 OutEp=0x2 InEp=0x81
```

## Next Steps

1. Audit xHCI Bulk IN completion semantics for `Ep=0x81` / `Dci=3`:
   - transfer-event transfer length interpretation.
   - short-packet handling on normal TRBs.
   - whether repeated IN retries still correlate to the correct transfer event.
2. Verify endpoint-context programming for the bulk IN endpoint after `Configure Endpoint`:
   - endpoint type.
   - MPS.
   - dequeue pointer.
   - endpoint state transitions observed after retries.
3. Verify transfer-ring progression after recovery:
   - whether software enqueue/cycle state and controller dequeue state remain synchronized after `STOP_ENDPOINT` / `RESET_ENDPOINT` / `SET_TR_DEQUEUE_POINTER`.
4. Confirm whether the mouse interrupt IN endpoint ever completes on Predator:
   - device discovery and report submission are confirmed.
   - report completion is not yet proven in the captured logs.
5. Keep the log filter focused on xHCI, BOT, and USB mouse tags so future screenshots stay readable.

## Latest Predator Result

Date: 2026-03-12

### Objective findings
- BOT progressed further than before:
  - `READ CAPACITY (10)` produced a real CSW instead of ending only in transport timeout.
  - `REQUEST SENSE` completed successfully at least once.
- Observed sense data:
  - `Response=0x70`
  - `SenseKey=0x6`
  - `ASC=0x28`
  - `ASCQ=0x0`
- A partition entry `u0p0` of type EFI became visible for the first time during this debug cycle.

### Interpretation
- The BOT path is no longer blocked only by missing xHCI completion events.
- At least one command sequence reached device-level BOT completion and valid sense reporting.
- `SenseKey=0x6 / ASC=0x28 / ASCQ=0x0` is consistent with a media-state change indication rather than a pure transport parse failure.
- The storage path remains unstable because later `READ CAPACITY`, data-stage, and CSW IN attempts can still fall back to timeout/recovery.

### Separate issue
- Port `14` still enters repeated probe failure loops unrelated to the BOT device on port `1`.
- `XHCI_LogProbeFailure` has been rate-limited so this unstable port does not flood the log indefinitely.

### Updated objective conclusion
- There is a real storage-progress signal on Predator:
  - valid BOT exchange observed,
  - valid sense data observed,
  - EFI partition visibility observed.
- The remaining blocker is instability of the Bulk IN path, not a total inability to communicate with the USB mass-storage device.

## Bulk IN Context Follow-up

Date: 2026-03-12

- Bulk endpoint context audit result:
  - no field remained clearly non-compliant;
  - one field was still not aligned with the xHCI recommended initial value:
    - `Endpoint Context DW4[15:0] Average TRB Length`
    - previous code used `MaxPacketSize` (`512` here)
    - spec guidance for bulk endpoints recommends `3KB`
- Status:
  - corrected in `XHCI_BuildBulkEndpointContext`
  - bulk endpoints now use `Average TRB Length = 3072`
