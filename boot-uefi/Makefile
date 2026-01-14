include ../make/arch.mk

BUILD_DIR := ../build/$(ARCH)
UEFI_BUILD_DIR := $(BUILD_DIR)/boot-uefi
IMG_SIZE_MB ?= 64

KERNEL_BIN := $(BUILD_DIR)/kernel/exos.bin
UEFI_IMAGE := $(UEFI_BUILD_DIR)/exos-uefi.img
UEFI_STAGING_DIR := $(UEFI_BUILD_DIR)/esp-root

ifeq ($(ARCH),i386)
UEFI_TARGET := i686-unknown-windows
UEFI_MACHINE := X86
UEFI_BOOT_FILE := BOOTIA32.EFI
UEFI_ARCH_SOURCE := ../boot-hd/source/vbr-payload-i386-uefi.c
UEFI_JUMP_SOURCE := source/uefi-jump-i386.asm
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

UEFI_ASM_OBJ := $(UEFI_BUILD_DIR)/uefi-jump-$(ARCH).o

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
	@echo "Creating UEFI disk image ($(IMG_SIZE_MB)MiB)..."
	@dd if=/dev/zero of=$(UEFI_IMAGE) bs=1M count=$(IMG_SIZE_MB)
	@parted -s $(UEFI_IMAGE) mklabel gpt
	@parted -s $(UEFI_IMAGE) mkpart ESP fat32 1MiB 100%
	@parted -s $(UEFI_IMAGE) set 1 esp on
	@{ \
		PART_OFFSET=$$(parted -s $(UEFI_IMAGE) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
			echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
			exit 1; \
		fi; \
		MTOOLS_SKIP_CHECK=1 mformat -i $(UEFI_IMAGE)@@$$PART_OFFSET -v EXOS -F ::; \
		MTOOLS_SKIP_CHECK=1 mcopy -i $(UEFI_IMAGE)@@$$PART_OFFSET -s $(UEFI_STAGING_DIR)/* ::/; \
	}

clean:
	@echo "[ Cleaning UEFI build ]"
	@rm -rf $(UEFI_BUILD_DIR)
