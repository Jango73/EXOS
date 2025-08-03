
CC      = i686-elf-gcc
LD      = i686-elf-ld
AS      = AS = i686-elf-as
NASM    = nasm

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O2 -fno-pic -fno-stack-protector -fno-builtin -Werror-implicit-function-declaration
AFLAGS  = -f elf -I source/

LDFLAGS = -T linker.ld -nostdlib -Map=bin/runtime.map

RUNTIME_LIB = bin/libexos.a

SRC_C  = $(wildcard source/*.c)
SRC_S  = $(wildcard source/*.asm)

OBJ_C  = $(patsubst source/%.c, bin/%.o, $(SRC_C))
OBJ_S  = $(patsubst source/%.asm, bin/%.o, $(SRC_S))

OBJS   = $(OBJ_S) $(OBJ_C)

# === Build rules ===

all: $(RUNTIME_LIB)

$(RUNTIME_LIB): $(OBJS)
	@mkdir -p $(dir $@)
	i686-elf-ar rcs $@ $^

bin/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

bin/%.o: source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

clean:
	rm -rf bin/*.o bin/*.elf bin/*.bin bin/*.a bin/*.map
