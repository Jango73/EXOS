# Makefile for EXOS BIOS image (partitioned MBR + FAT32 + custom MBR/VBR/Payload)

FINAL_IMG       = bin/exos.img
IMG_SIZE_MB     = 64
KERNEL_BIN      = ../kernel/bin/exos.bin
MTOOLS_CONF     = .mtoolsrc.tmp
CUSTOM_MBR      = source/mbr.asm
CUSTOM_MBR_BIN  = bin/mbr.bin

VBR_SRC         = source/vbr.asm
VBR_BIN         = bin/vbr.bin
SECTORS_TO_LOAD = 8         # Number of sectors loaded by VBR (including sector 0)
VBR_CODE_OFFSET = 0x5A      # Offset of code in VBR sector

PAYLOAD_STUB    = source/main-stub.asm
PAYLOAD_STUB_O  = bin/main-stub.o
PAYLOAD_C       = source/main.c
PAYLOAD_C_O     = bin/main.o
PAYLOAD_STR_C   = ../kernel/source/String.c
PAYLOAD_STR_O   = bin/String.o
PAYLOAD_BIN_LD  = main-bin.ld
PAYLOAD_ELF_LD  = main-elf.ld
PAYLOAD_BIN     = bin/payload.bin
PAYLOAD_ELF     = bin/payload.elf
PAYLOAD_MAP     = bin/payload.map

CFLAGS  = -m16 -g -Wall -Wextra -O2 \
	-ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
	-fvisibility=hidden -mno-red-zone \
	-Werror-implicit-function-declaration -Wstrict-prototypes

LD      		= i686-elf-ld
LDFLAGS 		= -nostdlib -no-pie

.PHONY: all update install-mbr install-vbr-payload clean check_tools hexdump-check rebuild-image

# Default: build tools, ensure image exists (create once), then update contents
all: check_tools $(FINAL_IMG) update install-mbr install-vbr-payload hexdump-check

# Create image only if it does not exist
$(FINAL_IMG):
	@echo "Creating $(IMG_SIZE_MB)MiB image with partition table and FAT32 partition..."
	dd if=/dev/zero of=$(FINAL_IMG) bs=1M count=$(IMG_SIZE_MB)
	parted -s $(FINAL_IMG) mklabel msdos
	parted -s $(FINAL_IMG) mkpart primary fat32 2048s 100%
	parted -s $(FINAL_IMG) set 1 boot on
	@{ \
		PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		echo "Partition offset is: $$PART_OFFSET"; \
		if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
			echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
			exit 1; \
		fi; \
		echo "Formatting partition using mformat..."; \
		MTOOLS_SKIP_CHECK=1 mformat -i $(FINAL_IMG)@@$$PART_OFFSET -v EXOS -F ::; \
	}

# Force a full rebuild of the image (explicit call)
rebuild-image:
	@echo "Forcing full image rebuild..."
	@rm -f $(FINAL_IMG)
	$(MAKE) $(FINAL_IMG)

update: $(FINAL_IMG)
	@rm -f $(MTOOLS_CONF)
	@{ \
		PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		echo "drive z: file=\"$(FINAL_IMG)\" offset=$$PART_OFFSET" > $(MTOOLS_CONF); \
		echo "Injecting files using mtools at offset $$PART_OFFSET"; \
		MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(KERNEL_BIN) z:/exos.bin; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/exos || true; \
		rm -f $(MTOOLS_CONF); \
		echo "All files injected into image."; \
	}

install-mbr: $(FINAL_IMG) $(CUSTOM_MBR_BIN)
	@echo "Installing custom MBR into image (assembled from $(CUSTOM_MBR))..."
	dd if=$(CUSTOM_MBR_BIN) of=$(FINAL_IMG) bs=446 count=1 conv=notrunc
	printf '\125\252' | dd of=$(FINAL_IMG) bs=1 seek=510 count=2 conv=notrunc
	@echo "Custom MBR written."

install-vbr-payload: $(FINAL_IMG) $(VBR_BIN) $(PAYLOAD_BIN) $(PAYLOAD_ELF)
	@echo "Injecting VBR code at offset 0x5A in VBR sector, and payload at the next sector..."
	@{ \
		PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		VBR_SIZE=$$(stat -c %s $(VBR_BIN)); \
		dd if=$(VBR_BIN) of=$(FINAL_IMG) bs=1 seek=$$((PART_OFFSET + $(VBR_CODE_OFFSET))) count=$$VBR_SIZE conv=notrunc; \
		SECTOR_NUM=$$((PART_OFFSET / 512 + 1)); \
		dd if=$(PAYLOAD_BIN) of=$(FINAL_IMG) bs=512 seek=$$SECTOR_NUM conv=notrunc; \
		echo "VBR code and payload injected."; \
	}

$(CUSTOM_MBR_BIN): $(CUSTOM_MBR)
	@echo "Assembling custom MBR (NASM)..."
	nasm -f bin $(CUSTOM_MBR) -o $(CUSTOM_MBR_BIN)

$(VBR_BIN): $(VBR_SRC)
	@echo "Assembling VBR (NASM)..."
	nasm -f bin $(VBR_SRC) -o $(VBR_BIN)

$(PAYLOAD_STUB_O): $(PAYLOAD_STUB)
	@echo "Assembling payload stub (NASM)..."
	nasm -f elf32 $< -o $@

$(PAYLOAD_C_O): $(PAYLOAD_C)
	@echo "Compiling payload C..."
	i686-elf-gcc $(CFLAGS) -c $< -o $@

$(PAYLOAD_STR_O): $(PAYLOAD_STR_C)
	@echo "Compiling String..."
	i686-elf-gcc -ffreestanding -m16 -nostdlib -fno-pic -fno-builtin -c $< -o $@

$(PAYLOAD_BIN): $(PAYLOAD_STUB_O) $(PAYLOAD_C_O) $(PAYLOAD_STR_O) $(PAYLOAD_BIN_LD)
	@echo "Linking payload stub + C into flat binary..."
	$(LD) -T $(PAYLOAD_BIN_LD) $(LDFLAGS) -o $@ $(PAYLOAD_STUB_O) $(PAYLOAD_C_O) $(PAYLOAD_STR_O)

$(PAYLOAD_ELF): $(PAYLOAD_STUB_O) $(PAYLOAD_C_O) $(PAYLOAD_STR_O) $(PAYLOAD_ELF_LD)
	@echo "Linking payload stub + C into elf binary..."
	$(LD) -T $(PAYLOAD_ELF_LD) $(LDFLAGS) -Map=$(PAYLOAD_MAP) -o $@ $(PAYLOAD_STUB_O) $(PAYLOAD_C_O) $(PAYLOAD_STR_O)

check_tools:
	@command -v nasm >/dev/null 2>&1 || { echo "nasm is not installed. Please run: sudo apt install nasm"; exit 1; }
	@command -v mcopy >/dev/null 2>&1 || { echo "mtools is not installed. Please run: sudo apt install mtools"; exit 1; }
	@command -v parted >/dev/null 2>&1 || { echo "parted is not installed. Please run: sudo apt install parted"; exit 1; }
	@command -v i686-elf-gcc >/dev/null 2>&1 || { echo "i686-elf-gcc is not installed. Please install your cross toolchain."; exit 1; }
	@command -v i686-elf-ld >/dev/null 2>&1 || { echo "i686-elf-ld is not installed. Please install your cross toolchain."; exit 1; }

hexdump-check:
	@echo "--- [ MBR +0 (16 bytes) ] ---"
	@hexdump -Cv -n 16 -s 0 $(FINAL_IMG)
	@echo "--- [ MBR code ] ---"
	@MBR_OFF=0; \
	dd if=$(FINAL_IMG) bs=1 skip=$$MBR_OFF count=32 2>/dev/null | ndisasm -b 16 -o 0x7E5A -
	@echo "--- [ MBR signature ] ---"
	@hexdump -Cv -n 2 -s 510 $(FINAL_IMG)

	@echo "--- [ VBR +0 (16 bytes) ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	hexdump -Cv -n 4 -s $$((PART_OFFSET + 0)) $(FINAL_IMG)
	@echo "--- [ VBR +0x5A (16 bytes) ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	hexdump -Cv -n 16 -s $$((PART_OFFSET + 0x5A)) $(FINAL_IMG)
	@echo "--- [ VBR code ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	PAYLOAD_OFF=$$((PART_OFFSET + 0x5A)); \
	dd if=$(FINAL_IMG) bs=1 skip=$$PAYLOAD_OFF count=32 2>/dev/null | ndisasm -b 16 -o 0x7E5A -
	@echo "--- [ VBR signature ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	hexdump -Cv -n 2 -s $$((PART_OFFSET + 510)) $(FINAL_IMG)

	@echo "--- [ Payload sector +0 (64 bytes) ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	PAYLOAD_OFF=$$((PART_OFFSET + 512)); \
	hexdump -Cv -n 64 -s $$PAYLOAD_OFF $(FINAL_IMG)
	@echo "--- [ Payload code ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	PAYLOAD_OFF=$$((PART_OFFSET + 512)); \
	dd if=$(FINAL_IMG) bs=1 skip=$$PAYLOAD_OFF count=16 2>/dev/null | ndisasm -b 16 -o 0x8000 -

clean:
	rm -f $(MTOOLS_CONF) $(CUSTOM_MBR_BIN) $(VBR_BIN) $(PAYLOAD_BIN) $(PAYLOAD_ELF) $(PAYLOAD_STUB_O) $(PAYLOAD_C_O)
