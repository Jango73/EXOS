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

CFLAGS  = -ffreestanding -Wall -Wextra -O0 -g -fno-pic -fno-stack-protector -fno-builtin -fcf-protection=none \
          $(ARCH_CFLAGS)

LDFLAGS = -T terminal-tactics.ld -nostdlib -Map=$(BUILD_DIR)/system/terminal-tactics/terminal-tactics.map $(ARCH_LDFLAGS)

TARGET   = $(BUILD_DIR)/system/terminal-tactics/terminal-tactics
TARGET_SYMBOLS = $(BUILD_DIR)/system/terminal-tactics/terminal-tactics.sym
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/system/terminal-tactics/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(TARGET) $(TARGET_SYMBOLS)

$(TARGET): $(OBJS) $(LIB_EXOS) terminal-tactics.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIB_EXOS)

$(BUILD_DIR)/system/terminal-tactics/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET_SYMBOLS): $(TARGET)
	@echo "Extracting terminal-tactics symbols for Bochs debugging"
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(BUILD_DIR)/system/terminal-tactics/*.o $(BUILD_DIR)/system/terminal-tactics/terminal-tactics $(BUILD_DIR)/system/terminal-tactics/*.elf $(BUILD_DIR)/system/terminal-tactics/*.map $(TARGET_SYMBOLS)
