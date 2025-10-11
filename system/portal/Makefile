################################################################################
#
#       EXOS System Programs
#       Copyright (c) 1999-2025 Jango73
#
################################################################################

ARCH ?= i386

BUILD_DIR := ../../build/$(ARCH)

ifeq ($(ARCH),i386)
TOOLCHAIN_PREFIX = i686-elf
CC      = $(TOOLCHAIN_PREFIX)-gcc
LD      = $(TOOLCHAIN_PREFIX)-ld
ARCH_CFLAGS =
ARCH_LDFLAGS =
else ifeq ($(ARCH),x86-64)
CC      = gcc
LD      = ld
ARCH_CFLAGS = -m64
ARCH_LDFLAGS = -m elf_x86_64
else
$(error Unsupported architecture $(ARCH))
endif

CFLAGS  = -ffreestanding -Wall -Wextra -O1 -fno-pic -fno-stack-protector -fno-builtin \
          $(ARCH_CFLAGS)

LDFLAGS = -T portal.ld -nostdlib -Map=$(BUILD_DIR)/system/portal/portal.map $(ARCH_LDFLAGS)

PORTAL  = $(BUILD_DIR)/system/portal/portal
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/system/portal/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(PORTAL)

$(PORTAL): $(OBJS) portal.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIB_EXOS)

$(BUILD_DIR)/system/portal/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)/system/portal/*.o $(BUILD_DIR)/system/portal/*.elf $(BUILD_DIR)/system/portal/*.bin $(BUILD_DIR)/system/portal/*.map
