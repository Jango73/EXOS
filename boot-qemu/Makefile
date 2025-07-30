NASM     = nasm
ASMFLAGS = -f bin
NODE     = node

KERNEL_BIN  = ../kernel/bin/exos.bin
DISK_IMG    = ./bin/disk.img
BOOT_BIN    = ./bin/boot.bin
SECTOR_INFO = kernel_sectors.inc

all: $(DISK_IMG)

# Step 1: generate kernel sector info
$(SECTOR_INFO): $(KERNEL_BIN) calcKernelSectors.js
	$(NODE) calcKernelSectors.js

# Step 2: assemble the bootloader with sector info
$(BOOT_BIN): boot-qemu.asm $(SECTOR_INFO)
	$(NASM) $(ASMFLAGS) -o $(BOOT_BIN) boot-qemu.asm

# Step 3: build the disk image and superblock
$(DISK_IMG): $(BOOT_BIN) superblock.js $(KERNEL_BIN)
	cp $(BOOT_BIN) $(DISK_IMG)
	$(NODE) superblock.js

clean:
	rm -f $(DISK_IMG) $(BOOT_BIN) $(SECTOR_INFO)
