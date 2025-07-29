NASM     = nasm
ASMFLAGS = -f bin
NODE     = node

KERNEL_BIN = ../kernel/bin/exos.bin
DISK_IMG   = ./bin/disk.img
BOOT_BIN   = ./bin/boot.bin

all: $(DISK_IMG)

# Étape 1 : Générer le SuperBlock + boot-info.txt
$(SECTOR_INFO): $(KERNEL_BIN) superblock.js
	$(NODE) superblock.js > $(SECTOR_INFO)

# Étape 2 : Assembler le bootloader avec la bonne info secteur
$(BOOT_BIN): boot-qemu.asm $(SECTOR_INFO)
	$(NASM) $(ASMFLAGS) -o $(BOOT_BIN) boot-qemu.asm

# Étape 3 : Construire l’image disque
$(DISK_IMG): $(BOOT_BIN)
	cp $(BOOT_BIN) $(DISK_IMG)
	dd if=/dev/zero bs=512 count=100 >> $(DISK_IMG) 2>/dev/null

clean:
	rm -f $(DISK_IMG) $(BOOT_BIN) $(SECTOR_INFO)
