# Makefile for EXOS BIOS image (partitioned MBR + FAT32 + custom MBR/VBR/Payload)

ARCH            ?= i386
DEBUG_OUTPUT    ?= 0

IMG_SIZE_MB     ?= 64
FILE_SYSTEM     ?= ext2
EXT2_BLOCK_SIZE ?= 4096
KERNEL_FILE     ?= exos.bin
PAYLOAD_OFFSET  ?= 0x8000
RESERVED_SECTORS= 64		# Reserved sectors for VBR payload
VBR_CODE_OFFSET = 0x5A		# Offset of code in VBR sector

BUILD_DIR       = ../build

KERNEL_CFG_FAT32= ../kernel/configuration/exos.fat32.toml
KERNEL_CFG_EXT2 = ../kernel/configuration/exos.ext2.toml
KERNEL_CFG_REF  = ../kernel/configuration/exos.ref.toml
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

NASMFLAGS       = -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -DRESERVED_SECTORS=$(RESERVED_SECTORS) -DPAYLOAD_OFFSET=$(PAYLOAD_OFFSET)
CFLAGS          = -m16 -g -Wall -Wextra -O0 \
                  -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie \
                  -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
                  -fvisibility=hidden -mno-red-zone -fno-stack-protector \
                  -mpreferred-stack-boundary=2 -mincoming-stack-boundary=2 \
                  -Werror-implicit-function-declaration -Wstrict-prototypes \
                  -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -DPAYLOAD_OFFSET=$(PAYLOAD_OFFSET) \
                  -DKERNEL_FILE=\"$(KERNEL_FILE)\" \
                  -Iinclude -I../kernel/include

ifeq ($(ARCH),i386)
ARCH_DEFINES    = -DBOOT_ARCH_I386
NASMFLAGS      += -DARCH_I386
else ifeq ($(ARCH),x86-64)
ARCH_DEFINES    = -DBOOT_ARCH_X86_64
NASMFLAGS      += -DARCH_X86_64
else
$(error Unsupported architecture $(ARCH))
endif

CFLAGS         += $(ARCH_DEFINES)
LD              = i686-elf-ld
LDFLAGS         = -nostdlib -no-pie
LDDEFS          = --defsym=PAYLOAD_OFFSET=$(PAYLOAD_OFFSET)

MBR_BIN         = $(BUILD_DIR)/boot-hd/mbr.bin
VBR_BIN         = $(BUILD_DIR)/boot-hd/vbr.bin
PAYLOAD_STUB_O  = $(BUILD_DIR)/boot-hd/vbr-payload-a.o
PAYLOAD_C_SOURCES = $(PAYLOAD_SOURCE) source/vbr-payload-fat32.c source/vbr-payload-ext2.c
PAYLOAD_C_OBJS  = $(patsubst source/%.c,$(BUILD_DIR)/boot-hd/%.o,$(sort $(PAYLOAD_C_SOURCES)))
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
EXT2_STAGING_DIR= $(BUILD_DIR)/boot-hd/ext2-root
EXT2_LABEL      ?= exos

define CREATE_FAT32_IMAGE
	@if [ -f $1 ]; then \
		echo "Skipping creation of $1 (already exists)."; \
	else \
		echo "Creating $(IMG_SIZE_MB)MiB image with partition table and FAT32 partition..."; \
		dd if=/dev/zero of=$1 bs=1M count=$(IMG_SIZE_MB); \
		parted -s $1 mklabel msdos; \
		parted -s $1 mkpart primary fat32 2048s 100%; \
		parted -s $1 set 1 boot on; \
		{ \
			PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
			if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
				echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
				exit 1; \
			fi; \
			MTOOLS_SKIP_CHECK=1 mformat -i $1@@$$PART_OFFSET -v EXOS -F -R $(RESERVED_SECTORS) ::; \
		}; \
	fi
endef

define CREATE_EXT2_IMAGE
	@if [ -f $1 ]; then \
		echo "Skipping creation of $1 (already exists)."; \
	else \
		echo "Creating $(IMG_SIZE_MB)MiB image with partition table and EXT2 partition..."; \
		dd if=/dev/zero of=$1 bs=1M count=$(IMG_SIZE_MB); \
		parted -s $1 mklabel msdos; \
		parted -s $1 mkpart primary ext2 2048s 100%; \
		parted -s $1 set 1 boot on; \
		{ \
			PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
			if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
				echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
				exit 1; \
			fi; \
                        mke2fs -F -t ext2 -b $(EXT2_BLOCK_SIZE) -q -L $(EXT2_LABEL) -E offset=$$PART_OFFSET $1; \
		}; \
	fi
endef

define POPULATE_EXT2_IMAGE
	@{ \
		PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
			echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
			exit 1; \
		fi; \
		echo "Populating EXT2 filesystem at offset $$PART_OFFSET from $2"; \
                mke2fs -F -t ext2 -b $(EXT2_BLOCK_SIZE) -q -L $(EXT2_LABEL) -d $2 -E offset=$$PART_OFFSET $1; \
	}
endef

ifeq ($(FILE_SYSTEM),ext2)
CREATE_IMAGE_MACRO = CREATE_EXT2_IMAGE
UPDATE_TARGET      = update-ext2
FS_SPECIFIC_CHECK  = @command -v mke2fs >/dev/null 2>&1 || { echo "mke2fs is not installed. Please run: sudo apt install e2fsprogs"; exit 1; }
else
CREATE_IMAGE_MACRO = CREATE_FAT32_IMAGE
UPDATE_TARGET      = update-fat32
FS_SPECIFIC_CHECK  = @command -v mcopy >/dev/null 2>&1 || { echo "mtools is not installed. Please run: sudo apt install mtools"; exit 1; }
endif

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

define MCOPY_IF_NEEDED
	if ! MTOOLSRC=$(MTOOLS_CONF) mdir "$(2)" >/dev/null 2>&1; then \
		echo "Copying $(1) -> $(2) (destination missing)"; \
		MTOOLSRC=$(MTOOLS_CONF) mcopy -o "$(1)" "$(2)"; \
	else \
		SRC_MTIME=$$(stat -c %Y "$(1)"); \
		DST_INFO=$$(MTOOLSRC=$(MTOOLS_CONF) mdir -/ "$(2)" 2>/dev/null | awk 'NF>=7 {print $$6" "$$7; exit}'); \
		DST_EPOCH=$$(date -d "$$DST_INFO" +%s 2>/dev/null || echo ""); \
		if [ -z "$$DST_EPOCH" ] || [ $$SRC_MTIME -gt $$DST_EPOCH ]; then \
			if [ -z "$$DST_EPOCH" ]; then \
				echo "Copying $(1) -> $(2) (destination timestamp unavailable)"; \
			else \
				echo "Updating $(2) from $(1) (source newer)"; \
			fi; \
			MTOOLSRC=$(MTOOLS_CONF) mcopy -o "$(1)" "$(2)"; \
		else \
			echo "Skipping $(2) (destination up-to-date)"; \
		fi; \
	fi;
endef

.PHONY: all update update-fat32 update-ext2 install-mbr install-vbr-payload clean check_tools rebuild-image

# Default: build tools, ensure images exist (create once), then update contents
all: check_tools $(FINAL_IMG) update install-vbr-payload $(BUILD_DIR)/boot-hd/payload.sym

check_tools:
	@command -v nasm >/dev/null 2>&1 || { echo "nasm is not installed. Please run: sudo apt install nasm"; exit 1; }
	@command -v parted >/dev/null 2>&1 || { echo "parted is not installed. Please run: sudo apt install parted"; exit 1; }
	@command -v i686-elf-gcc >/dev/null 2>&1 || { echo "i686-elf-gcc is not installed. Please install your cross toolchain."; exit 1; }
	@command -v i686-elf-ld >/dev/null 2>&1 || { echo "i686-elf-ld is not installed. Please install your cross toolchain."; exit 1; }
	$(FS_SPECIFIC_CHECK)

$(MBR_BIN): $(MBR_SOURCE)
	@echo "Assembling custom MBR (NASM)..."
	@mkdir -p $(BUILD_DIR)/boot-hd
	nasm $(NASMFLAGS) -f bin $(MBR_SOURCE) -o $(MBR_BIN)

$(VBR_BIN): $(VBR_SRC)
	@echo "Assembling VBR (NASM)..."
	@mkdir -p $(BUILD_DIR)/boot-hd
	nasm $(NASMFLAGS) -f bin $(VBR_SRC) -o $(VBR_BIN)

$(PAYLOAD_STUB_O): $(PAYLOAD_STUB)
	@echo "Assembling payload stub (NASM)..."
	@mkdir -p $(BUILD_DIR)/boot-hd
	nasm $(NASMFLAGS) -f elf32 $< -o $@

$(BUILD_DIR)/boot-hd/%.o: source/%.c
	@echo "Compiling payload C from $<..."
	@mkdir -p $(BUILD_DIR)/boot-hd
	i686-elf-gcc $(CFLAGS) -c $< -o $@

$(PAYLOAD_STR_O): $(PAYLOAD_STR_C)
	@echo "Compiling String from $(PAYLOAD_STR_C)..."
	@mkdir -p $(BUILD_DIR)/boot-hd
	i686-elf-gcc $(CFLAGS) -c $< -o $@

$(PAYLOAD_BIN): $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O) $(PAYLOAD_BIN_LD)
	@echo "Linking payload stub + C into flat binary..."
	$(LD) $(LDDEFS) -T $(PAYLOAD_BIN_LD) $(LDFLAGS) -o $@ $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O)

$(PAYLOAD_ELF): $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O) $(PAYLOAD_ELF_LD)
	@echo "Linking payload stub + C into elf binary..."
	$(LD) $(LDDEFS) -T $(PAYLOAD_ELF_LD) $(LDFLAGS) -Map=$(PAYLOAD_MAP) -o $@ $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O)

$(BUILD_DIR)/boot-hd/payload.sym: $(PAYLOAD_ELF)
	# @echo "Extracting payload symbols for Bochs debugging"
	i686-elf-nm $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

# Create image only if it does not exist
$(FINAL_IMG): check_tools $(KERNEL_BIN) $(MBR_BIN) $(VBR_BIN) $(PAYLOAD_BIN) $(PORTAL_ELF) $(NETGET_ELF) $(HELLO_ELF) $(TICTACTOE_ELF) $(MASTER_ELF) $(SLAVE_ELF)
	@mkdir -p $(dir $@)
	$(call $(CREATE_IMAGE_MACRO),$@)

$(KERNEL_CFG_FAT32): $(FINAL_IMG)
	@[ -f $@ ] || cp $(KERNEL_CFG_REF) $@

$(KERNEL_CFG_EXT2): $(FINAL_IMG)
	@[ -f $@ ] || cp $(KERNEL_CFG_REF) $@

update: $(UPDATE_TARGET)

update-fat32: $(FINAL_IMG) $(KERNEL_CFG_FAT32)
	$(call MTOOLS_OPERATION,$(FINAL_IMG),Injecting files using mtools,\
		$(call MCOPY_IF_NEEDED,$(KERNEL_BIN),z:/EXOS.BIN) \
		$(call MCOPY_IF_NEEDED,$(KERNEL_CFG_FAT32),z:/exos.toml) \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/USERS || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS/TEST || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/TEMP || true; \
		$(call MCOPY_IF_NEEDED,$(PORTAL_ELF),z:/EXOS/APPS/PORTAL) \
		$(call MCOPY_IF_NEEDED,$(NETGET_ELF),z:/EXOS/APPS/NETGET) \
		$(call MCOPY_IF_NEEDED,$(HELLO_ELF),z:/EXOS/APPS/HELLO)\
		$(call MCOPY_IF_NEEDED,$(TICTACTOE_ELF),z:/EXOS/APPS/tictactoe)\
		$(call MCOPY_IF_NEEDED,$(MASTER_ELF),z:/EXOS/APPS/TEST/MASTER)\
		$(call MCOPY_IF_NEEDED,$(SLAVE_ELF),z:/EXOS/APPS/TEST/SLAVE),\
		All files injected into image.)

update-ext2: $(FINAL_IMG) $(KERNEL_CFG_EXT2)
	@echo "Preparing EXT2 staging directory at $(EXT2_STAGING_DIR)"
	@rm -rf $(EXT2_STAGING_DIR)
	@mkdir -p $(EXT2_STAGING_DIR)/exos/data
	@mkdir -p $(EXT2_STAGING_DIR)/exos/users
	@mkdir -p $(EXT2_STAGING_DIR)/exos/apps/test
	@mkdir -p $(EXT2_STAGING_DIR)/exos/temp
	@cp $(KERNEL_BIN) $(EXT2_STAGING_DIR)/exos.bin
	@cp $(KERNEL_CFG_EXT2) $(EXT2_STAGING_DIR)/exos.toml
	@cp $(PORTAL_ELF) $(EXT2_STAGING_DIR)/exos/apps/portal
	@cp $(NETGET_ELF) $(EXT2_STAGING_DIR)/exos/apps/netget
	@cp $(HELLO_ELF) $(EXT2_STAGING_DIR)/exos/apps/hello
	@cp $(TICTACTOE_ELF) $(EXT2_STAGING_DIR)/exos/apps/tictactoe
	@cp $(MASTER_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/master
	@cp $(SLAVE_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/slave
	$(call POPULATE_EXT2_IMAGE,$(FINAL_IMG),$(EXT2_STAGING_DIR))

install-mbr: update $(MBR_BIN) $(VBR_BIN)
	@echo "Installing custom MBR into image (assembled from $(MBR_SOURCE))..."
	dd if=$(MBR_BIN) of=$(FINAL_IMG) bs=446 count=1 conv=notrunc
	printf '\125\252' | dd of=$(FINAL_IMG) bs=1 seek=510 count=2 conv=notrunc
	@echo "Custom MBR written."

install-vbr-payload: install-mbr $(PAYLOAD_BIN) $(VBR_BIN)
	@echo "Injecting VBR code at offset 0x5A in VBR sector, and payload into the pre-partition gap..."
	@{ \
		PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		VBR_SIZE=$$(stat -c %s $(VBR_BIN)); \
		PART_SECTOR=$$((PART_OFFSET / 512)); \
		PAYLOAD_SIZE=$$(stat -c %s $(PAYLOAD_BIN)); \
		PAYLOAD_SECTORS_ACTUAL=$$(( (PAYLOAD_SIZE + 511) / 512 )); \
		if [ $$PAYLOAD_SECTORS_ACTUAL -gt $(RESERVED_SECTORS) ]; then \
			echo "ERROR: Payload uses $$PAYLOAD_SECTORS_ACTUAL sectors but only $(RESERVED_SECTORS) are reserved."; \
			exit 1; \
		fi; \
		PAYLOAD_START=$$((PART_SECTOR - $(RESERVED_SECTORS))); \
		if [ $$PAYLOAD_START -lt 1 ]; then \
			echo "ERROR: Not enough gap before partition to store payload."; \
			exit 1; \
		fi; \
		dd if=/dev/zero of=$(FINAL_IMG) bs=512 seek=$$PAYLOAD_START count=$(RESERVED_SECTORS) conv=notrunc; \
		dd if=$(PAYLOAD_BIN) of=$(FINAL_IMG) bs=1 seek=$$((PAYLOAD_START * 512)) conv=notrunc; \
		dd if=$(VBR_BIN) of=$(FINAL_IMG) bs=1 seek=$$((PART_OFFSET + $(VBR_CODE_OFFSET))) count=$$VBR_SIZE conv=notrunc; \
		printf '\125\252' | dd of=$(FINAL_IMG) bs=1 seek=$$((PART_OFFSET + 510)) count=2 conv=notrunc; \
		echo "VBR code and payload injected."; \
	}

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
	PAYLOAD_SECTOR_START=$$((PART_OFFSET / 512 - $(RESERVED_SECTORS))); \
	PAYLOAD_OFF=$$((PAYLOAD_SECTOR_START * 512)); \
	hexdump -Cv -n 64 -s $$PAYLOAD_OFF $(FINAL_IMG)
	@echo "--- [ Payload code ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	PAYLOAD_SECTOR_START=$$((PART_OFFSET / 512 - $(RESERVED_SECTORS))); \
	PAYLOAD_OFF=$$((PAYLOAD_SECTOR_START * 512)); \
	dd if=$(FINAL_IMG) bs=1 skip=$$PAYLOAD_OFF count=16 2>/dev/null | ndisasm -b 16 -o $(PAYLOAD_OFFSET) -

clean:
	rm -f $(MTOOLS_CONF) $(MBR_BIN) $(VBR_BIN) $(PAYLOAD_BIN) $(PAYLOAD_ELF) $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O) $(BUILD_DIR)/boot-hd/payload.sym
