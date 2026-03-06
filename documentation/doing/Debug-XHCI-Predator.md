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
  - run `STOP_ENDPOINT` + `RESET_ENDPOINT` on the target DCI.
  - reset software transfer ring state before retry.
  - keep USB `CLEAR_FEATURE(ENDPOINT_HALT)` as protocol-level recovery step.
  - files:
    - `kernel/source/drivers/usb/XHCI-Device-Lifecycle.c`
    - `kernel/source/drivers/storage/USBStorage-Transport.c`

## External Driver Cross-Check
- Linux xHCI behavior was used as reference for normal TRB direction semantics.
- No Predator-specific mandatory quirk for `Intel 8086:a36d` was identified from that check for this exact symptom.
- Working hypothesis remains implementation gap in EXOS xHCI/BOT path, not a proven required hardware quirk.

## Status
- Bulk OUT path reaches device.
- Bulk IN completion for BOT data stage (`Ep=0x81`) times out on Predator during READ CAPACITY and REQUEST SENSE.
- xHCI stack is partially functional (device visible, BOT init started) but storage attach remains blocked on bulk IN transfer completion.

## Next Debug Focus
- Instrument transfer event matching for `Ep=0x81`:
  - verify transfer event pointer/correlation against enqueued TRB physical address.
  - verify completion code and transfer length fields from event TRB when timeout path triggers.
- Verify endpoint context fields for DCI of bulk IN after `Configure Endpoint`:
  - endpoint state, dequeue pointer, MPS, endpoint type.
- Keep log filter focused on xHCI + USB storage tags to preserve screenshot readability.

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

Date: 2026-03-06 (local debug run)

### What the log sequence confirms
- BOT initialization starts correctly:
  - `INQUIRY (Op=0x12)` succeeds.
- The first failing operation is `READ CAPACITY (10)`:
  - `Op=0x25`, data stage `DirIn=1`, `Len=8`, endpoint `Ep=0x81` (`Dci=3`).
  - `USBStorageWaitCompletion` reaches timeout (`Elapsed=1000`).

### xHCI recovery chain observed on each failed attempt
- `XHCI_ResetTransferEndpoint` starts for `Slot=0x1`, `DCI=0x3`.
- `XHCI_StopEndpoint` returns a completion code in logs (visible per attempt).
- `XHCI_SetTransferRingDequeuePointer` returns:
  - `Completion=0x13` (repeated).
  - `Dequeue` value logged as programmed pointer (example seen: `...:0x618001`).
- Endpoint context snapshots around recovery are visible:
  - Before recovery: `CtxState` observed as `0x2` and later `0x1` depending on attempt.
  - After recovery: `CtxState=0x3` in the captured lines.
  - `CtxDequeue` differs from programmed dequeue in captured lines (example seen: context `...:0x618021` vs programmed `...:0x618001`).

### Practical interpretation
- The failure is concentrated on Bulk IN endpoint recovery (DCI 3), not on generic BOT command construction.
- `SET_TR_DEQUEUE_POINTER` is consistently rejected (`Completion=0x13`) during recovery.
- Because recovery does not complete, the same `READ CAPACITY` data-stage timeout pattern repeats and then cascades into `REQUEST SENSE` failures.

### Current working conclusion
- Primary blocker is in xHCI recovery semantics/state handling for Bulk IN (`Ep=0x81`, `Dci=3`) on Predator:
  - command acceptance/state preconditions and/or dequeue pointer synchronization with endpoint context remain unresolved.

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
`-> USBStorageWaitCompletion(Timeout Elapsed=1000)`  
`-> XHCI_ResetTransferEndpoint(Begin, CtxState/CtxDequeue logged)`  
`-> XHCI_StopEndpoint(Completion seen, often 0x1)`  
`-> XHCI_SetTransferRingDequeuePointer(Completion=0x13, repeated)`  
`-> XHCI_ResetTransferEndpoint(End Success=0, SetCompletion=0x13)`  
`-> retry repeats same failure chain`  
`-> USBStorageBotCommand Data stage failed Op=0x25`  
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
T6860> DEBUG > [USBStorageBotCommand] Op=0x25 CdbLen=10 DataLen=8 DirIn=1 Tag=0x2 ...
T7160> DEBUG > [XHCI_ResetTransferEndpoint] Begin Slot=0x1 DCI=0x3 Ep=0x81 Halted=1 CtxState=0x2 CtxDequeue=0x0:0x618021 ...
T7240> DEBUG > [XHCI_SetTransferRingDequeuePointer] Slot=0x1 DCI=0x3 Completion=0x13 Dequeue=0x0:0x618001 ...
T7240> DEBUG > [XHCI_ResetTransferEndpoint] End Slot=0x1 DCI=0x3 Success=0 StopCompletion=0x0 SetCompletion=0x13 ProgrammedDequeue=0x0:0x618001 CtxState=0x3 CtxDequeue=0x0:0x618021 ...
T49060> DEBUG > [USBStorageWaitCompletion] Timeout Elapsed=1000 Trb=0x0:0x618000 suppressed=13
T49060> DEBUG > [USBStorageBulkTransferOnce] Timeout Op=0x25 Slot=0x1 Port=1 Addr=1 Ep=0x81 Dci=3 DirIn=1 Len=8 Trb=...
T49060> DEBUG > [USBStorageBulkTransfer] Attempt=1/3 failed Slot=0x1 Port=1 Addr=1 Ep=0x81 DirIn=1 ...
T49100> DEBUG > [XHCI_ResetTransferEndpoint] Begin Slot=0x1 DCI=0x3 Ep=0x81 Halted=0 CtxState=0x1 CtxDequeue=0x0:0x618021 ...
T49100> DEBUG > [XHCI_StopEndpoint] Slot=0x1 DCI=0x3 Completion=0x1 Trb=...
T49120> DEBUG > [XHCI_SetTransferRingDequeuePointer] Slot=0x1 DCI=0x3 Completion=0x13 Dequeue=0x0:0x618001 ...
T49140> DEBUG > [XHCI_ResetTransferEndpoint] End Slot=0x1 DCI=0x3 Success=0 StopCompletion=0x1 SetCompletion=0x13 ProgrammedDequeue=0x0:0x618001 CtxState=0x3 CtxDequeue=0x0:0x618021 ...
T90940> DEBUG > [USBStorageWaitCompletion] Timeout Elapsed=1000 Trb=0x0:0x618000 suppressed=1
T90940> DEBUG > [USBStorageBulkTransferOnce] Timeout Op=0x25 Slot=0x1 Port=1 Addr=1 Ep=0x81 Dci=3 DirIn=1 Len=8 Trb=...
T90940> DEBUG > [XHCI_ResetTransferEndpoint] Begin Slot=0x1 DCI=0x3 Ep=0x81 Halted=0 CtxState=0x1 CtxDequeue=0x0:0x618021 ...
T90980> DEBUG > [XHCI_StopEndpoint] Slot=0x1 DCI=0x3 Completion=0x1 Trb=...
T91020> DEBUG > [XHCI_SetTransferRingDequeuePointer] Slot=0x1 DCI=0x3 Completion=0x13 Dequeue=0x0:0x618001 ...
T91060> DEBUG > [XHCI_ResetTransferEndpoint] End Slot=0x1 DCI=0x3 Success=0 StopCompletion=0x1 SetCompletion=0x13 ProgrammedDequeue=0x0:0x618001 CtxState=0x3 CtxDequeue=0x0:0x618021 ...
T91060> ERROR > [USBStorageBotCommand] Data stage failed Op=0x25 Tag=0x2 DirIn=1 Len=8
T91060> WARNING > [USBStorageStartDevice] READ CAPACITY failed, attempting reset
T91060> DEBUG > [USBStorageBotCommand] Op=0x3 CdbLen=6 DataLen=18 DirIn=1 Tag=0x3 ...
```

## Next Steps

1. Verify the exact meaning of `Completion=0x13` against the local xHCI completion-code table.
2. Verify `XHCI_SetTransferRingDequeuePointer` TRB encoding (fields, reserved bits, DCS handling).
3. Verify context index usage for `DCI=3` (off-by-one risks between slot/control/endpoint contexts).
4. Compare `ProgrammedDequeue` vs `CtxDequeue` after command while masking/isolating cycle-related bits and alignment constraints.
5. Run one minimal recovery variant at a time:
   - keep software ring state unchanged before `SET_TR_DEQUEUE_POINTER`, or
   - program dequeue from current endpoint context instead of ring base,
   - then retry `Op=0x25` once.
6. If `0x13` persists, capture completion of `RESET_ENDPOINT` in the same sequence to validate endpoint state preconditions before `SET_TR_DEQUEUE_POINTER`.
