
CC      = i686-elf-gcc
LD      = i686-elf-ld
AS      = AS = i686-elf-as
NASM	= nasm

CFLAGS  = -g -ffreestanding -m32 -Wall -Wextra -O0 -fno-pic -fno-stack-protector -fno-builtin
AFLAGS  = -f elf -I source/

LDFLAGS_BIN = -T linker-bin.ld -nostdlib -Map=bin/exos-bin.map
LDFLAGS_ELF = -T linker-elf.ld -nostdlib -Map=bin/exos-elf.map

KERNEL_ELF = bin/exos.elf
KERNEL_BIN = bin/exos.bin

SRC_C  = $(wildcard source/*.c)
SRC_S  = $(wildcard source/*.asm)

OBJ_C  = $(patsubst source/%.c, bin/%.o, $(SRC_C))
OBJ_S  = $(patsubst source/%.asm, bin/%.o, $(SRC_S))

OBJS   = $(OBJ_S) $(OBJ_C)

# === Build rules ===

all: $(KERNEL_BIN) $(KERNEL_ELF)

$(KERNEL_BIN): $(OBJS) linker-bin.ld
	$(LD) $(LDFLAGS_BIN) -o $@ $(OBJS)

$(KERNEL_ELF): $(OBJS) linker-elf.ld
	$(LD) $(LDFLAGS_ELF) -o $@ $(OBJS)

bin/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

bin/%.o: source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

clean:
	rm -rf bin/*.o bin/*.elf bin/*.bin bin/*.map
