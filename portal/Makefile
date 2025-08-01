
CC      = i686-elf-gcc
LD      = i686-elf-ld
AS      = AS = i686-elf-as
NASM    = nasm

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O2 -fno-pic -fno-stack-protector -fno-builtin

LDFLAGS = -T portal.ld -nostdlib -Map=bin/portal.map

PORTAL  = bin/portal.bin

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, bin/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

# === Build rules ===

all: $(PORTAL)

$(PORTAL): $(OBJS) portal.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) ../runtime/bin/libexos.a

bin/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf bin/*.o bin/*.elf bin/*.bin bin/*.map
