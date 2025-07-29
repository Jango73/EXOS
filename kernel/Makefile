# === Compiler toolchain ===

CC      = i686-elf-gcc
LD      = i686-elf-ld
AS      = AS = i686-elf-as
NASM	= nasm
OBJCOPY = i686-elf-objcopy

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O2 -fno-pic -fno-stack-protector -fno-builtin
AFLAGS  = -f elf -I source/

LDFLAGS = -T linker.ld -nostdlib

KERNEL_ELF = bin/exos.elf
KERNEL_BIN = bin/exos.bin

SRC_C  = $(wildcard source/*.c)
SRC_S  = $(wildcard source/*.asm)

OBJ_C  = $(patsubst source/%.c, bin/%.o, $(SRC_C))
OBJ_S  = $(patsubst source/%.asm, bin/%.o, $(SRC_S))

OBJS   = $(OBJ_S) $(OBJ_C)

# === Build rules ===

all: $(KERNEL_BIN)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(KERNEL_ELF): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

bin/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

bin/%.o: source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

clean:
	rm -rf bin/*.o bin/*.elf bin/*.bin
