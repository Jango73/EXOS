include ../make/arch.mk

BUILD_DIR := ../build/$(ARCH)
UEFI_BUILD_DIR := $(BUILD_DIR)/boot-uefi
IMG_SIZE_MB ?= 16
ESP_SIZE_MB ?= 4
EXT2_BLOCK_SIZE ?= 4096
EXT2_LABEL ?= exos
CONFIG_TOML_NAME ?= exos.toml
UEFI_EARLY_HALT ?= 0
UEFI_STUB_TEST ?= 0
UEFI_STUB_EARLY_CALL ?= 0
UEFI_STUB_REPLACE ?= 0
UEFI_STUB_C_FALLBACK ?= 0

KERNEL_BIN := $(BUILD_DIR)/kernel/exos.bin
UEFI_IMAGE := $(UEFI_BUILD_DIR)/exos-uefi.img
UEFI_STAGING_DIR := $(UEFI_BUILD_DIR)/esp-root
EXT2_STAGING_DIR := $(UEFI_BUILD_DIR)/ext2-root

KERNEL_CFG_EXT2 := ../kernel/configuration/exos.ext2.toml
KERNEL_CFG_REF  := ../kernel/configuration/exos.ref.toml
KEYBOARD_LAYOUTS := ../deploy/keyboard/*.ekm1

PORTAL_ELF      := $(BUILD_DIR)/system/portal/portal
NETGET_ELF      := $(BUILD_DIR)/system/netget/netget
HELLO_ELF       := $(BUILD_DIR)/system/hello/hello
TICTACTOE_ELF   := $(BUILD_DIR)/system/tictactoe/tictactoe
TERMTACTICS_ELF := $(BUILD_DIR)/system/terminal-tactics/terminal-tactics
MOUSEINFO_ELF   := $(BUILD_DIR)/system/mouse-info/mouse-info
MASTER_ELF      := $(BUILD_DIR)/system/test/master/master
SLAVE_ELF       := $(BUILD_DIR)/system/test/slave/slave

ifeq ($(ARCH),x86-32)
UEFI_TARGET := i686-unknown-windows
UEFI_MACHINE := X86
UEFI_BOOT_FILE := BOOTIA32.EFI
UEFI_ARCH_SOURCE := ../boot-hd/source/vbr-payload-x86-32-uefi.c
UEFI_JUMP_SOURCE := source/uefi-jump-x86-32.asm
NASM_FORMAT := win32
else ifeq ($(ARCH),x86-64)
UEFI_TARGET := x86_64-unknown-windows
UEFI_MACHINE := X64
UEFI_BOOT_FILE := BOOTX64.EFI
UEFI_ARCH_SOURCE := ../boot-hd/source/vbr-payload-x86-64-uefi.c
UEFI_JUMP_SOURCE := source/uefi-jump-x86-64.asm
NASM_FORMAT := win64
else
$(error Unsupported architecture $(ARCH))
endif

CC := clang
LD := lld-link
NASM := nasm

CFLAGS := -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie \
          -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
          -mno-red-zone -Wall -Wextra -Werror-implicit-function-declaration -Wstrict-prototypes \
          -DBOOT_UEFI=1 -DPAYLOAD_ADDRESS=0x8000 -DMEMORY_BASE=0x10000 -DMEMORY_SIZE=0x80000 \
          -DCONFIG_VMA_KERNEL=$(VMA_KERNEL) \
          -DUEFI_EARLY_HALT=$(UEFI_EARLY_HALT) \
          -DUEFI_STUB_TEST=$(UEFI_STUB_TEST) \
          -DUEFI_STUB_EARLY_CALL=$(UEFI_STUB_EARLY_CALL) \
          -DUEFI_STUB_REPLACE=$(UEFI_STUB_REPLACE) \
          -DUEFI_STUB_C_FALLBACK=$(UEFI_STUB_C_FALLBACK) \
          -DARCH=$(ARCH) \
          -Iinclude -I../boot-hd/include -I../boot-shared/include -I../kernel/include

LDFLAGS := /subsystem:efi_application /entry:EfiMain /nodefaultlib /dll /machine:$(UEFI_MACHINE)

UEFI_BOOT_FILE_PATH := $(UEFI_BUILD_DIR)/$(UEFI_BOOT_FILE)

UEFI_C_SOURCES := source/boot-uefi.c \
                  source/uefi-compat.c \
                  ../boot-shared/source/boot-multiboot.c \
                  ../kernel/source/CoreString.c \
                  $(UEFI_ARCH_SOURCE)

UEFI_C_OBJS := $(UEFI_BUILD_DIR)/boot-uefi.o \
               $(UEFI_BUILD_DIR)/uefi-compat.o \
               $(UEFI_BUILD_DIR)/boot-multiboot.o \
               $(UEFI_BUILD_DIR)/CoreString.o \
               $(UEFI_BUILD_DIR)/vbr-payload-$(ARCH).o

ifeq ($(UEFI_STUB_C_FALLBACK),1)
UEFI_ASM_OBJ :=
else
UEFI_ASM_OBJ := $(UEFI_BUILD_DIR)/uefi-jump-$(ARCH).o
endif

.PHONY: all clean check_tools image

all: check_tools $(UEFI_BOOT_FILE_PATH) $(UEFI_IMAGE)

image: $(UEFI_IMAGE)

check_tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "clang is not installed."; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "lld-link is not installed."; exit 1; }
	@command -v $(NASM) >/dev/null 2>&1 || { echo "nasm is not installed."; exit 1; }
	@command -v mformat >/dev/null 2>&1 || { echo "mtools is not installed."; exit 1; }
	@command -v mcopy >/dev/null 2>&1 || { echo "mtools is not installed."; exit 1; }
	@command -v parted >/dev/null 2>&1 || { echo "parted is not installed."; exit 1; }
	@command -v mke2fs >/dev/null 2>&1 || { echo "mke2fs is not installed. Please run: sudo apt install e2fsprogs"; exit 1; }

$(UEFI_BUILD_DIR):
	@mkdir -p $(UEFI_BUILD_DIR)

$(UEFI_BUILD_DIR)/boot-uefi.o: source/boot-uefi.c | $(UEFI_BUILD_DIR)
	@echo "Compiling UEFI loader..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/boot-multiboot.o: ../boot-shared/source/boot-multiboot.c | $(UEFI_BUILD_DIR)
	@echo "Compiling Multiboot helpers..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/uefi-compat.o: source/uefi-compat.c | $(UEFI_BUILD_DIR)
	@echo "Compiling UEFI compat..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/CoreString.o: ../kernel/source/CoreString.c | $(UEFI_BUILD_DIR)
	@echo "Compiling CoreString..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/vbr-payload-$(ARCH).o: $(UEFI_ARCH_SOURCE) | $(UEFI_BUILD_DIR)
	@echo "Compiling paging helpers..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_ASM_OBJ): $(UEFI_JUMP_SOURCE) | $(UEFI_BUILD_DIR)
	@echo "Assembling UEFI jump stub..."
	$(NASM) -f $(NASM_FORMAT) $< -o $@

$(UEFI_BOOT_FILE_PATH): $(UEFI_C_OBJS) $(UEFI_ASM_OBJ)
	@echo "Linking UEFI bootloader..."
	$(LD) $(LDFLAGS) /out:$@ $(UEFI_C_OBJS) $(UEFI_ASM_OBJ)

$(UEFI_IMAGE): $(UEFI_BOOT_FILE_PATH) $(KERNEL_BIN)
	@mkdir -p $(UEFI_BUILD_DIR)
	@rm -rf $(UEFI_STAGING_DIR)
	@mkdir -p $(UEFI_STAGING_DIR)/EFI/BOOT
	@cp -f $(UEFI_BOOT_FILE_PATH) $(UEFI_STAGING_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE)
	@cp -f $(KERNEL_BIN) $(UEFI_STAGING_DIR)/exos.bin
	@[ -f $(KERNEL_CFG_EXT2) ] || cp $(KERNEL_CFG_REF) $(KERNEL_CFG_EXT2)
	@echo "Preparing EXT2 staging directory at $(EXT2_STAGING_DIR)"
	@rm -rf $(EXT2_STAGING_DIR)
	@mkdir -p $(EXT2_STAGING_DIR)/exos/data
	@mkdir -p $(EXT2_STAGING_DIR)/exos/users
	@mkdir -p $(EXT2_STAGING_DIR)/exos/apps/test
	@mkdir -p $(EXT2_STAGING_DIR)/exos/keyboard
	@mkdir -p $(EXT2_STAGING_DIR)/exos/temp
	@cp $(KERNEL_BIN) $(EXT2_STAGING_DIR)/exos.bin
	@cp $(KERNEL_CFG_EXT2) $(EXT2_STAGING_DIR)/$(CONFIG_TOML_NAME)
	@for f in $(KEYBOARD_LAYOUTS); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $(EXT2_STAGING_DIR)/exos/keyboard/$$(basename $$f)"; \
			cp "$$f" "$(EXT2_STAGING_DIR)/exos/keyboard/$$(basename $$f)"; \
		fi; \
	done
	@cp $(PORTAL_ELF) $(EXT2_STAGING_DIR)/exos/apps/portal
	@cp $(NETGET_ELF) $(EXT2_STAGING_DIR)/exos/apps/netget
	@cp $(HELLO_ELF) $(EXT2_STAGING_DIR)/exos/apps/hello
	@cp $(TICTACTOE_ELF) $(EXT2_STAGING_DIR)/exos/apps/tictactoe
	@cp $(TERMTACTICS_ELF) $(EXT2_STAGING_DIR)/exos/apps/terminal-tactics
	@cp $(MOUSEINFO_ELF) $(EXT2_STAGING_DIR)/exos/apps/mouse-info
	@cp $(MASTER_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/master
	@cp $(SLAVE_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/slave
	@echo "Creating UEFI disk image ($(IMG_SIZE_MB)MiB)..."
	@dd if=/dev/zero of=$(UEFI_IMAGE) bs=1M count=$(IMG_SIZE_MB)
	@parted -s $(UEFI_IMAGE) mklabel gpt
	@parted -s $(UEFI_IMAGE) mkpart ESP fat32 1MiB $(ESP_SIZE_MB)MiB
	@parted -s $(UEFI_IMAGE) mkpart EXOS ext2 $(ESP_SIZE_MB)MiB 100%
	@parted -s $(UEFI_IMAGE) set 1 esp on
	@{ \
		PART1_OFFSET=$$(parted -s $(UEFI_IMAGE) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		PART2_OFFSET=$$(parted -s $(UEFI_IMAGE) unit B print | awk '/^ 2/ { gsub("B","",$$2); print $$2; exit }'); \
		if [ -z "$$PART1_OFFSET" ] || [ "$$PART1_OFFSET" = "0" ]; then \
			echo "ERROR: ESP partition not found, or PART1_OFFSET is zero. Aborting."; \
			exit 1; \
		fi; \
		if [ -z "$$PART2_OFFSET" ] || [ "$$PART2_OFFSET" = "0" ]; then \
			echo "ERROR: EXT2 partition not found, or PART2_OFFSET is zero. Aborting."; \
			exit 1; \
		fi; \
		MTOOLS_SKIP_CHECK=1 mformat -i $(UEFI_IMAGE)@@$$PART1_OFFSET -v EXOS ::; \
		MTOOLS_SKIP_CHECK=1 mcopy -i $(UEFI_IMAGE)@@$$PART1_OFFSET -s $(UEFI_STAGING_DIR)/* ::/; \
		echo "Populating EXT2 filesystem at offset $$PART2_OFFSET from $(EXT2_STAGING_DIR)"; \
		mke2fs -F -t ext2 -b $(EXT2_BLOCK_SIZE) -q -L $(EXT2_LABEL) -d $(EXT2_STAGING_DIR) -E offset=$$PART2_OFFSET $(UEFI_IMAGE); \
	}

clean:
	@echo "[ Cleaning UEFI build ]"
	@rm -rf $(UEFI_BUILD_DIR)
