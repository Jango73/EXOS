# Boot/Network Checksum Mismatch - Incident Notes

## Scope
This folder collects artifacts for the intermittent `CHECKSUM MISMATCH` symptom observed during x86-64 smoke attempts on 2026-02-26.

## What was observed
- During repeated runs of:
  - `bash scripts/4-1-smoke-test.sh --only x86-64 --stop-after-shell --no-build`
- The kernel log contains:
  - `DEBUG > [TCP_ValidateChecksum] CHECKSUM MISMATCH - packet may be corrupted`

Observed occurrences captured in this folder:
- `20260226-122311-x86-64-pass-kernel.log` (line 576)
- `20260226-122316-x86-64-pass-kernel.log` (line 576)
- `20260226-122320-x86-64-pass-kernel.log` (line 576)

See extracted lines in:
- `mismatch-lines.txt`

## Screenshot Transcription (manual capture)
Transcription of the visible text from the provided QEMU screenshot:

```text
SeaBIOS (version rel-1.16.3-0-ga6ed6b701f0a-prebuilt.qemu.org)

iPXE (http://ipxe.org) 00:08.0 CA00 PCI2.10 PnP PMM+06FC8110+06F28110 CA00

Booting from Hard Disk...
Loading VBR
Jumping to VBR
Loading payload...
Jumping to VBR-2 code
[VBR C Stub] Jumping to BootMain
[VBR] E820 map at 0xeb60
[VBR] E820 entry count : 9
[VBR] Loading and running binary OS at 0x00200000
[VBR] Probing FAT32 filesystem
[VBR] Probing EXT2 filesystem
[VBR] EXT2 kernel size 0x0014C000 bytes
[VBR] Kernel loaded via EXT2
[VBR] VerifyKernelImage scanning 1359868 data bytes
[VBR] Last 8 bytes of file: 0x534f5845 0x022f3ee
[VBR] Stored checksum in image : 0x922f3ee
[VBR] Checksum mismatch. Halting. Stored : 0x922f3ee vs computed : 0x9229c80
```

## About the interrupted run
- The campaign was manually interrupted during `smoke-x86-64-campaign-4.log`.
- Because the run was cut mid-attempt, no final archived pair was produced for that specific attempt.
- Snapshot files copied here after interruption:
  - `kernel-x86-64-mbr-debug.log` (empty at snapshot time)
  - `debug-com1-x86-64-mbr-debug.log` (empty at snapshot time)

## Follow-up observation (attempt index variability)
- In a later validation series, the same `Checksum mismatch. Halting.` symptom appeared again but at **attempt 4** (not attempt 11).
- Conclusion: attempt index is not fixed. The trigger is likely an intermittent race/state issue during chained runs, not a deterministic "always Nth attempt" rule.

## Working hypothesis and next technical step
- This is likely a state/race bug, not pure randomness.
- The current VBR message is sufficient to locate the failing stage:
  - `Checksum mismatch. Halting. Stored ... vs computed ...`
- Next useful instrumentation (when resumed):
  - log exact EXT2 read ranges/LBA sequence used by VBR to load/verify kernel
  - log verify span boundaries and intermediate checksum checkpoints
  - compare one successful run and one failing run at VBR level

## Included artifacts
- Runner outputs from interrupted campaign attempts:
  - `smoke-x86-64-campaign-1.log`
  - `smoke-x86-64-campaign-2.log`
  - `smoke-x86-64-campaign-3.log`
  - `smoke-x86-64-campaign-4.log`
- Kernel logs showing mismatch:
  - `20260226-122311-x86-64-pass-kernel.log`
  - `20260226-122316-x86-64-pass-kernel.log`
  - `20260226-122320-x86-64-pass-kernel.log`
- Extracted mismatch lines:
  - `mismatch-lines.txt`
- Post-interrupt snapshots:
  - `kernel-x86-64-mbr-debug.log`
  - `debug-com1-x86-64-mbr-debug.log`
