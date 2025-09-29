################################################################################
#
#	EXOS System Programs
#	Copyright (c) 1999-2025 Jango73
#
################################################################################

BUILD_DIR = ../../build

CC      = i686-elf-gcc
LD      = i686-elf-ld
AS      = AS = i686-elf-as
NASM    = nasm

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O1 -fno-pic -fno-stack-protector -fno-builtin

LDFLAGS = -T portal.ld -nostdlib -Map=$(BUILD_DIR)/system/portal/portal.map

PORTAL  = $(BUILD_DIR)/system/portal/portal

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/system/portal/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

# === Build rules ===

all: $(PORTAL)

$(PORTAL): $(OBJS) portal.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(BUILD_DIR)/runtime/libexos.a

$(BUILD_DIR)/system/portal/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)/system/portal/*.o $(BUILD_DIR)/system/portal/*.elf $(BUILD_DIR)/system/portal/*.bin $(BUILD_DIR)/system/portal/*.map
