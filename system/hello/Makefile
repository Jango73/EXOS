################################################################################
#
#	EXOS System Programs
#	Copyright (c) 1999-2025 Jango73
#
################################################################################

BUILD_DIR = ../../build

CC      = i686-elf-gcc
LD      = i686-elf-ld

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O0 -fno-pic -fno-stack-protector -fno-builtin

LDFLAGS = -T hello.ld -nostdlib -Map=$(BUILD_DIR)/system/hello/hello.map

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
	i686-elf-nm $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(BUILD_DIR)/system/hello/*.o $(BUILD_DIR)/system/hello/*.elf $(BUILD_DIR)/system/hello/*.map $(HELLO_SYMBOLS)
