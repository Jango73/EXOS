CC      = i686-elf-gcc
LD      = i686-elf-ld

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O2 -fno-pic -fno-stack-protector -fno-builtin

LDFLAGS = -T hello.ld -nostdlib -Map=bin/hello.map

HELLO   = bin/hello.elf

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, bin/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(HELLO)

$(HELLO): $(OBJS) hello.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) ../runtime/bin/libexos.a

bin/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf bin/*.o bin/*.elf bin/*.map
