################################################################################
#
#       EXOS System Programs
#       Copyright (c) 1999-2025 Jango73
#
################################################################################

ARCH ?= i386

BUILD_DIR := ../../../build/$(ARCH)

ifeq ($(ARCH),i386)
TOOLCHAIN_PREFIX = i686-elf
CC      = $(TOOLCHAIN_PREFIX)-gcc
LD      = $(TOOLCHAIN_PREFIX)-ld
NM      = $(TOOLCHAIN_PREFIX)-nm
ARCH_CFLAGS =
ARCH_LDFLAGS =
else ifeq ($(ARCH),x86-64)
CC      = gcc
LD      = ld
NM      = nm
ARCH_CFLAGS = -m64
ARCH_LDFLAGS = -m elf_x86_64
else
$(error Unsupported architecture $(ARCH))
endif

CFLAGS  = -ffreestanding -Wall -Wextra -O0 -fno-pic -fno-stack-protector -fno-builtin -fcf-protection=none \
          $(ARCH_CFLAGS)

LDFLAGS = -T slave.ld -nostdlib -Map=$(BUILD_DIR)/system/test/slave/slave.map $(ARCH_LDFLAGS)

SLAVE   = $(BUILD_DIR)/system/test/slave/slave
SLAVE_SYMBOLS = $(BUILD_DIR)/system/test/slave/slave.sym
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/system/test/slave/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(SLAVE) $(SLAVE_SYMBOLS)

$(SLAVE): $(OBJS) $(LIB_EXOS) slave.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIB_EXOS)

$(BUILD_DIR)/system/test/slave/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(SLAVE_SYMBOLS): $(SLAVE)
	@echo "Extracting slave symbols for Bochs debugging"
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(BUILD_DIR)/system/test/slave/*.o $(BUILD_DIR)/system/test/slave/slave $(BUILD_DIR)/system/test/slave/*.elf $(BUILD_DIR)/system/test/slave/*.map $(SLAVE_SYMBOLS)
