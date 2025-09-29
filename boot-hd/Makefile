# Makefile for EXOS BIOS image (partitioned MBR + FAT32 + custom MBR/VBR/Payload)

DEBUG_OUTPUT    ?= 0

BUILD_DIR       = ../build

IMG_SIZE_MB     = 64
RESERVED_SECTORS= 48		# Reserved sectors in partition
VBR_CODE_OFFSET = 0x5A		# Offset of code in VBR sector

KERNEL_CFG      = exos.toml
KERNEL_CFG_REF  = exos.ref.toml
MTOOLS_CONF     = .mtoolsrc.tmp
MBR_SOURCE      = source/mbr.asm

VBR_SRC         = source/vbr.asm
PAYLOAD_STUB    = source/vbr-payload-a.asm
PAYLOAD_C       = source/vbr-payload-c.c
PAYLOAD_STR_C   = ../kernel/source/String.c

# Configurable payload source (can be overridden with make PAYLOAD_SOURCE=...)
PAYLOAD_SOURCE  ?= source/vbr-payload-c.c

PAYLOAD_BIN_LD  = main-bin.ld
PAYLOAD_ELF_LD  = main-elf.ld

NASMFLAGS       = -DDEBUG_OUTPUT=$(DEBUG_OUTPUT)
CFLAGS          = -m16 -g -Wall -Wextra -O0 \
                  -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie \
                  -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
                  -fvisibility=hidden -mno-red-zone -fno-stack-protector \
                  -mpreferred-stack-boundary=2 -mincoming-stack-boundary=2 \
                  -Werror-implicit-function-declaration -Wstrict-prototypes \
                  -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) \
                  -DKERNEL_FILE="\"$(KERNEL_FILE)\"" \
                  -Iinclude
LD              = i686-elf-ld
LDFLAGS         = -nostdlib -no-pie

MBR_BIN         = $(BUILD_DIR)/boot-hd/mbr.bin
VBR_BIN         = $(BUILD_DIR)/boot-hd/vbr.bin
PAYLOAD_STUB_O  = $(BUILD_DIR)/boot-hd/vbr-payload-a.o
PAYLOAD_C_O     = $(BUILD_DIR)/boot-hd/vbr-payload-c.o
PAYLOAD_STR_O   = $(BUILD_DIR)/boot-hd/String.o
PAYLOAD_BIN     = $(BUILD_DIR)/boot-hd/vbr-payload.bin
PAYLOAD_ELF     = $(BUILD_DIR)/boot-hd/vbr-payload.elf
PAYLOAD_MAP     = $(BUILD_DIR)/boot-hd/vbr-payload.map
FINAL_IMG       = $(BUILD_DIR)/boot-hd/exos.img
KERNEL_BIN      = $(BUILD_DIR)/kernel/exos.bin
PORTAL_ELF      = $(BUILD_DIR)/system/portal/portal
NETGET_ELF      = $(BUILD_DIR)/system/netget/netget
HELLO_ELF       = $(BUILD_DIR)/system/hello/hello
TICTACTOE_ELF	= $(BUILD_DIR)/system/tictactoe/tictactoe
MASTER_ELF      = $(BUILD_DIR)/system/test/master/master
SLAVE_ELF       = $(BUILD_DIR)/system/test/slave/slave

define CREATE_FAT32_IMAGE
        @echo "Creating $(IMG_SIZE_MB)MiB image with partition table and FAT32 partition..."
        dd if=/dev/zero of=$1 bs=1M count=$(IMG_SIZE_MB)
        parted -s $1 mklabel msdos
        parted -s $1 mkpart primary fat32 2048s 100%
        parted -s $1 set 1 boot on
        @{ \
                PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
                if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
                        echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
                        exit 1; \
                fi; \
                MTOOLS_SKIP_CHECK=1 mformat -i $1@@$$PART_OFFSET -v EXOS -F -R $(RESERVED_SECTORS) ::; \
        }
endef

define MTOOLS_OPERATION
        @rm -f $(MTOOLS_CONF)
        @{ \
                PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
                echo "drive z: file=\"$1\" offset=$$PART_OFFSET" > $(MTOOLS_CONF); \
                echo "$2 at offset $$PART_OFFSET"; \
                $3 \
                rm -f $(MTOOLS_CONF); \
                echo "$4"; \
        }
endef

.PHONY: all update install-mbr install-vbr-payload clean check_tools rebuild-image

# Default: build tools, ensure images exist (create once), then update contents
all: check_tools $(FINAL_IMG) update install-vbr-payload $(BUILD_DIR)/boot-hd/payload.sym

check_tools:
	@command -v nasm >/dev/null 2>&1 || { echo "nasm is not installed. Please run: sudo apt install nasm"; exit 1; }
	@command -v mcopy >/dev/null 2>&1 || { echo "mtools is not installed. Please run: sudo apt install mtools"; exit 1; }
	@command -v parted >/dev/null 2>&1 || { echo "parted is not installed. Please run: sudo apt install parted"; exit 1; }
	@command -v i686-elf-gcc >/dev/null 2>&1 || { echo "i686-elf-gcc is not installed. Please install your cross toolchain."; exit 1; }
	@command -v i686-elf-ld >/dev/null 2>&1 || { echo "i686-elf-ld is not installed. Please install your cross toolchain."; exit 1; }

# Create image only if it does not exist
$(FINAL_IMG): check_tools
	@mkdir -p $(dir $@)
	$(call CREATE_FAT32_IMAGE,$@)

$(KERNEL_CFG): $(FINAL_IMG)
	@[ -f $@ ] || cp $(KERNEL_CFG_REF) $@

# Force a full rebuild of the image (explicit call)
rebuild-image:
	@echo "Forcing full image rebuild..."
	@rm -f $(FINAL_IMG)
	$(MAKE) $(FINAL_IMG)

update: $(KERNEL_CFG) $(KERNEL_BIN) $(PORTAL_ELF) $(NETGET_ELF) $(HELLO_ELF) $(TICTACTOE_ELF) $(MASTER_ELF) $(SLAVE_ELF)
	$(call MTOOLS_OPERATION,$(FINAL_IMG),Injecting files using mtools,\
	        MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(KERNEL_BIN) z:/EXOS.BIN; \
	        MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(KERNEL_CFG) z:/exos.toml; \
	        MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS || true; \
	        MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/USERS || true; \
	        MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS || true; \
	        MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS/TEST || true; \
	        MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(PORTAL_ELF) z:/EXOS/APPS/PORTAL; \
	        MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(NETGET_ELF) z:/EXOS/APPS/NETGET; \
	        MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(HELLO_ELF) z:/EXOS/APPS/HELLO;\
	        MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(TICTACTOE_ELF) z:/EXOS/APPS/tictactoe;\
	        MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(MASTER_ELF) z:/EXOS/APPS/TEST/MASTER;\
	        MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(SLAVE_ELF) z:/EXOS/APPS/TEST/SLAVE;,\
	        All files injected into image.)

install-mbr: update $(MBR_BIN) $(VBR_BIN)
	@echo "Installing custom MBR into image (assembled from $(MBR_SOURCE))..."
	dd if=$(MBR_BIN) of=$(FINAL_IMG) bs=446 count=1 conv=notrunc
	printf '\125\252' | dd of=$(FINAL_IMG) bs=1 seek=510 count=2 conv=notrunc
	@echo "Custom MBR written."

install-vbr-payload: install-mbr $(PAYLOAD_BIN)
	@echo "Injecting VBR code at offset 0x5A in VBR sector, and payload at the next sector..."
	@{ \
		PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		VBR_SIZE=$$(stat -c %s $(VBR_BIN)); \
		dd if=$(VBR_BIN) of=$(FINAL_IMG) bs=1 seek=$$((PART_OFFSET + $(VBR_CODE_OFFSET))) count=$$VBR_SIZE conv=notrunc; \
		SECTOR_NUM=$$((PART_OFFSET / 512 + 1)); \
		dd if=$(PAYLOAD_BIN) of=$(FINAL_IMG) bs=512 seek=$$SECTOR_NUM conv=notrunc; \
		echo "VBR code and payload injected."; \
	}

$(MBR_BIN): $(MBR_SOURCE)
	@echo "Assembling custom MBR (NASM)..."
	nasm $(NASMFLAGS) -f bin $(MBR_SOURCE) -o $(MBR_BIN)

$(VBR_BIN): $(VBR_SRC)
	@echo "Assembling VBR (NASM)..."
	nasm $(NASMFLAGS) -f bin $(VBR_SRC) -o $(VBR_BIN)

$(PAYLOAD_STUB_O): $(PAYLOAD_STUB)
	@echo "Assembling payload stub (NASM)..."
	nasm $(NASMFLAGS) -f elf32 $< -o $@

$(PAYLOAD_C_O): $(PAYLOAD_SOURCE)
	@echo "Compiling payload C from $(PAYLOAD_SOURCE)..."
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

# Extract payload symbols for Bochs debugging
$(BUILD_DIR)/boot-hd/payload.sym: $(PAYLOAD_ELF)
	# @echo "Extracting payload symbols for Bochs debugging"
	i686-elf-nm $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

hexdump-check: install-vbr-payload
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
	rm -f $(MTOOLS_CONF) $(MBR_BIN) $(VBR_BIN) $(PAYLOAD_BIN) $(PAYLOAD_ELF) $(PAYLOAD_STUB_O) $(PAYLOAD_C_O) $(BUILD_DIR)/boot-hd/payload.sym
