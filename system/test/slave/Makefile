################################################################################
#
#	EXOS System Programs
#	Copyright (c) 1999-2025 Jango73
#
################################################################################

BUILD_DIR = ../../../build

CC      = i686-elf-gcc
LD      = i686-elf-ld

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O0 -fno-pic -fno-stack-protector -fno-builtin

LDFLAGS = -T slave.ld -nostdlib -Map=$(BUILD_DIR)/system/test/slave/slave.map

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
	i686-elf-nm $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(BUILD_DIR)/system/test/slave/*.o $(BUILD_DIR)/system/test/slave/*.elf $(BUILD_DIR)/system/test/slave/*.map $(SLAVE_SYMBOLS)
