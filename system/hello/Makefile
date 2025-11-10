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

LDFLAGS = -T hello.ld -nostdlib -Map=$(BUILD_DIR)/system/hello/hello.map $(ARCH_LDFLAGS)

HELLO   = $(BUILD_DIR)/system/hello/hello
HELLO_SYMBOLS = $(BUILD_DIR)/system/hello/hello.sym
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/system/hello/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(HELLO) $(HELLO_SYMBOLS)

$(HELLO): $(OBJS) $(LIB_EXOS) hello.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIB_EXOS)

$(BUILD_DIR)/system/hello/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(HELLO_SYMBOLS): $(HELLO)
	@echo "Extracting hello symbols for Bochs debugging"
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(BUILD_DIR)/system/hello/*.o $(BUILD_DIR)/system/hello/hello $(BUILD_DIR)/system/hello/*.elf $(BUILD_DIR)/system/hello/*.map $(HELLO_SYMBOLS)
