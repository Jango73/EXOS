CC      = i686-elf-gcc
LD      = i686-elf-ld
NASM    = nasm

CFLAGS  = -g -ffreestanding -m32 -Wall -Wextra -O0 -fno-pic -fno-stack-protector -fno-builtin -Werror-implicit-function-declaration
AFLAGS  = -f elf -g -F dwarf -I source/asm/

LDFLAGS_ELF = -T linker-elf.ld -nostdlib -Map=bin/exos-elf.map

KERNEL_ELF = bin/exos.elf

SRC_C  = $(wildcard source/*.c)
SRC_S  = $(wildcard source/asm/*.asm)

OBJ_C  = $(patsubst source/%.c, bin/%.o, $(SRC_C))
OBJ_S  = $(patsubst source/asm/%.asm, bin/%.o, $(SRC_S))

OBJS   = $(OBJ_S) $(OBJ_C)

# Sections to extract (edit as needed)
STUB_BIN   = bin/stub.bin
TEXT_BIN   = bin/text.bin
DATA_BIN   = bin/data.bin
BSS_BIN    = bin/bss.bin
FINAL_IMG  = bin/exos.bin

all: $(FINAL_IMG) check_magic

$(KERNEL_ELF): $(OBJS) linker-elf.ld
	$(LD) $(LDFLAGS_ELF) -o $@ $(OBJS)

# Extract each section as a flat binary
$(STUB_BIN): $(KERNEL_ELF)
	@echo "Extracting .stub section to $@"
	objcopy -O binary --only-section=.stub $< $@

$(TEXT_BIN): $(KERNEL_ELF)
	@echo "Extracting .text section to $@"
	objcopy -O binary --only-section=.text $< $@

$(DATA_BIN): $(KERNEL_ELF)
	@echo "Extracting .data section to $@"
	objcopy -O binary --only-section=.data $< $@

$(BSS_BIN): $(KERNEL_ELF)
	@echo "Extracting .bss section to $@"
	objcopy -O binary --only-section=.bss $< $@

# Build final padded image
$(FINAL_IMG): $(STUB_BIN) $(TEXT_BIN) $(DATA_BIN) $(BSS_BIN)
	@echo "Building padded disk image: $@"
	cat $(STUB_BIN) > $@
	# Pad to 0x1000 for .text (change if your layout differs)
	dd if=/dev/zero bs=1 count=$$(expr 4096 - $$(stat -c %s $(STUB_BIN))) >> $@ 2>/dev/null
	cat $(TEXT_BIN) >> $@
	# Pad to next 0x1000 for .data (edit as needed)
	dd if=/dev/zero bs=1 count=$$(expr 4096 - $$(stat -c %s $(STUB_BIN))) >> $@ 2>/dev/null
	cat $(DATA_BIN) >> $@
	# .bss is typically not written to disk (allocated as zero at runtime)
	@true

bin/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

bin/%.o: source/asm/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

check_magic: $(FINAL_IMG)
	@node check-exos-magic.js $(FINAL_IMG)

clean:
	rm -rf bin/*.o bin/*.elf bin/*.bin bin/*.map bin/*.img
