UEFI Bootloader Plan

Scope
- Add a native UEFI bootloader without external dependencies.
- Preserve the existing BIOS MBR/VBR path.
- Reuse the current VBR payload logic as much as possible, with refactors where needed.
- Keep the kernel entry contract unchanged (Multiboot information in registers).

Current Integration Points
- BIOS boot path lives in `boot-hd/` and produces `build/<arch>/boot-hd/exos.img`.
- The VBR payload builds Multiboot data in `boot-hd/source/vbr-payload-c.c` and jumps to `KernelMain` with `MULTIBOOT_BOOTLOADER_MAGIC`.
- The kernel expects Multiboot data in `kernel/source/Main.c`.

Key Reuse Targets
- Multiboot info builder in `boot-hd/source/vbr-payload-c.c`.
- FAT32 and EXT2 kernel loading in `boot-hd/source/vbr-payload-fat32.c` and `boot-hd/source/vbr-payload-ext2.c`.
- Common string and memory helpers already used by the bootloader.

Plan
1) Bootloader Core Refactor
   - Extract bootloader logic that does not depend on BIOS real mode into a new shared module.
   - Split by responsibility:
     - Boot console output.
     - Block device reads.
     - Memory map collection.
     - Multiboot info construction and kernel verification.
   - Define a narrow "boot platform" interface (function pointers or struct of callbacks).
   - Provide a BIOS adapter that wraps the current INT 0x13 and E820 logic.
   - Keep the existing VBR entry and payload flow, but route through the new core interface.

2) UEFI Bootloader Implementation
   - Create a new UEFI bootloader folder, for example `boot-uefi/`, with a freestanding C entry point.
   - Implement `EfiMain` and use UEFI Boot Services for:
     - Console output.
     - Memory map retrieval (convert UEFI memory descriptors to Multiboot memory entries).
     - File loading from the EFI System Partition (FAT32).
   - Provide a UEFI adapter that satisfies the boot platform interface from step 1.
   - Reuse the shared core to build Multiboot info and jump to the kernel entry.
   - Keep the kernel load address and entry path identical to the BIOS path.

3) Filesystem Strategy
   - Initial scope: load `exos.bin` from the EFI System Partition (FAT32).
   - Optional later scope: use Block IO to reuse the existing EXT2 loader to load the kernel from an EXT2 partition.
   - This keeps the first deliverable minimal while still aligned with existing code.

4) Build System Integration
   - Add a UEFI build target that produces `BOOTX64.EFI` (x86-64) and `BOOTIA32.EFI` (i386).
   - Use a dedicated linker script for PE32+ output.
   - Keep the current `boot-hd` make flow intact.
   - Add a new image output, for example `build/<arch>/boot-uefi/exos-uefi.img`.

5) USB Image Creation
   - Add a new create-USB script, similar to `scripts/<arch>/7-1-create-usb-ext2.sh`, to write a GPT image with:
     - An EFI System Partition (FAT32) containing `EFI/BOOT/BOOTX64.EFI` or `EFI/BOOT/BOOTIA32.EFI` plus `exos.bin`.
   - One USB image per architecture.
   - Reuse the existing `scripts/utils/create-usb-common.sh` confirmation and safety checks.

6) Documentation and Compatibility
   - Document the UEFI boot flow in `documentation/Kernel.md` once the code exists.
   - Keep both BIOS and UEFI boot paths working in parallel.

Assumptions Confirmed
- Target architecture for UEFI: x86-64 and i386.
- UEFI-only boot (no CSM) is required.
- Kernel file remains `exos.bin` and is placed on the EFI System Partition.
- Architecture coupling is strict: `BOOTX64.EFI` loads x86-64 `exos.bin`, `BOOTIA32.EFI` loads i386 `exos.bin`.

Deliverable Order
1) Core refactor and BIOS adapter in place (no behavior change).
2) UEFI loader builds and boots in QEMU with OVMF.
3) GPT USB creation script and local boot validation.
