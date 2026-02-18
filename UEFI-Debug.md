UEFI Debug Notes (Predator Laptop)
==================================

Date: 2026-01-30

Context
-------
- Target: x86-64, UEFI boot on bare metal (Predator laptop)
- QEMU UEFI boots successfully
- Build: debug split (`./scripts/build --arch x86-64 --fs ext2 --debug --split`)

UDP log capture (RJ45)
----------------------
- Build with UDP logger enabled:
  - `./scripts/build --arch x86-64 --fs ext2 --debug --split --clean --uefi --use-log-udp --log-udp-source 192.168.50.2:18195 --log-udp-dest 192.168.50.1:18194`
- Listen on the workstation:
  - `./scripts/utils/uefi-udp-log-listen.sh --bind 0.0.0.0 --port 18194`
- Optional raw capture:
  - `./scripts/utils/uefi-udp-log-listen.sh --port 18194 --raw /tmp/uefi-udp.log`

Observed Color Markers (baseline)
---------------------------------
On bare metal:
- Yellow (UEFI step 1)
- Green (UEFI step 2)
- Nothing (UEFI step 3)
- Blue (UEFI step 4)
- White (payload step 1)
- Cyan (payload step 2)
- Magenta (payload step 3)

Interpretation of current markers
---------------------------------
- Yellow/Green/Blue are from `BootUefiFramebufferMark` in UEFI.
- White/Cyan/Magenta are from `PayloadFramebufferMark` inside `EnterProtectedPagingAndJump`.
- The absence of the red marker (step 3) indicates no `ExitBootServices` retry path.
- The payload markers confirm `EnterProtectedPagingAndJump` is reached and executes up to the stub call.

Tests Performed
--------------
1) Allow framebuffer > 4GB and map PML4 > 0:
   - `MapIdentityRange` updated to allocate PDPT per PML4 index.
   - `BootUefiFramebufferMark` uses full 64-bit framebuffer address.
   - Result: no change on bare metal (same color sequence).

2) Additional payload markers before stub jump:
   - Added white/cyan/magenta markers in payload.
   - Result: all three visible on bare metal, so payload runs and reaches stub call.

3) Stub parameter stack offset change:
   - Tried stack offsets `[rsp+0x20]/[rsp+0x28]` for multiboot ptr/magic.
   - Result: regression on QEMU; reverted and left a "do not change" comment.

4) UEFI-only halt after `ExitBootServices` (UEFI_EARLY_HALT):
   - Added marker (white) then halt before paging/jump.
   - Result on bare metal: Yellow, Green, Nothing, White (halt), so UEFI path reaches ExitBootServices.

5) Stub pre-CR3 marker using multiboot info:
   - Added orange marker in stub before CR3 switch.
   - Result: visible on QEMU, not visible on bare metal.

6) Stub pre-CR3 marker independent of multiboot info:
   - Added globals for framebuffer base/pitch/bpp and used them in stub.
   - Result: still visible on QEMU, not visible on bare metal.

7) Stub test mode (UEFI_STUB_TEST=1):
   - Stub should draw orange and halt immediately.
   - Result on bare metal: no orange. Indicates stub code is not executing on bare metal
     (or execution jumps to an invalid address before the stub starts).

8) Added UEFI_STUB_EARLY_CALL flag (pending result):
   - Calls `EnterProtectedPagingAndJump` in stub test mode before ExitBootServices.
   - Goal: verify stub execution even before ExitBootServices to isolate call boundary issues.
   - Result on bare metal: no markers at all. Indicates control never reaches visible marker
     once early stub call is enabled, suggesting a call boundary/relocation issue rather than
     ExitBootServices.

9) Added serial trace + UD2 after EnterProtectedPagingAndJump:
   - UEFI prints serial before call and after return; UD2 traps if it ever returns.
   - Goal: confirm whether the call is made and whether control returns.

10) Added UEFI_STUB_REPLACE flag (pending result):
    - Replaces the stub call with a payload-side marker (step 4, green) + halt.
    - Goal: confirm that the call path is fine and isolate the assembly stub as the fault.
    - Result on bare metal: the extra green marker appears; confirms the call path is fine and
      isolates the failure to the assembly stub execution itself.

11) Added UEFI_STUB_C_FALLBACK flag (pending result):
    - Removes the NASM stub from the UEFI build and provides a C fallback `StubJumpToImage`
      that draws marker step 4 then halts.
    - Goal: isolate whether the NASM stub object itself is the root cause on bare metal.
    - Result on bare metal: extra green marker appears (same as QEMU), so call boundary works.
      This confirms the NASM stub object/code path is the failure point on bare metal.

12) Remove serial output from stub (pending result):
    - Deleted all serial routines/calls and removed .rodata strings/hex table in
      `boot-uefi/source/uefi-jump-x86-64.asm`.
    - Goal: eliminate serial I/O side effects and .rodata relocations without changing control flow.
    - Result on bare metal: orange marker now appears; stub executes. Still stops before reaching
      LongModeEntry (no post-CR3 or long-mode marker).

13) Move stub markers to distinct lines (pending result):
    - Pre-CR3 marker moved to Y=160, post-CR3 marker moved to Y=180 (both X=20).
    - Goal: avoid overlap with existing UEFI/payload markers and remove ordering ambiguity.
    - Result: pre-CR3 marker appears on bare metal at Y=160; post-CR3 marker does not.

14) Added CR3 pre-test marker (pending result):
    - Marker at Y=200 drawn just before `mov cr3`.
    - Goal: determine if the crash happens at the CR3 load or after it.

15) Use full 64-bit UEFI ImageBase (pending result):
    - `BootUefiOpenRootFileSystem` now stores `LoadedImage->ImageBase` without truncation.
    - Goal: ensure paging maps the UEFI image correctly when it is above 4GB on bare metal.
    - Result on bare metal: boot progresses; console log now appears (fix confirmed).

16) Map framebuffer as write-combining (pending result):
    - Added `MapFramebufferMemory` using `ALLOC_PAGES_WC` and switched console to use it.
    - Goal: speed up framebuffer writes for kernel log on bare metal.

17) Initialize PAT for WC (pending result):
    - Added PAT MSR initialization to map PAT entry 1 to WC.
    - Goal: ensure `ALLOC_PAGES_WC` results in real write-combining on bare metal.

Conclusion So Far
-----------------
- UEFI boot path and ExitBootServices complete successfully on bare metal.
- `EnterProtectedPagingAndJump` is reached and runs its C-side marker code.
- The x86-64 jump stub (`StubJumpToImage`) does not execute on bare metal.
- The failure is therefore between the call site and entry into the stub:
  likely ABI/calling convention mismatch or an incorrect function pointer/relocation
  at the call boundary (not inside the stub itself).

Next Focus
----------
- Verify the actual address used for `StubJumpToImage` on bare metal.
- Confirm the calling convention and object format alignment for the stub object.
- Add a serial-only trace right before the call and a forced `ud2` after the call
  to validate control flow and address integrity.
