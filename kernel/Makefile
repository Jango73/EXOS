SHELL   = /bin/bash

DEBUG_OUTPUT    ?= 0
NASMFLAGS       = -DDEBUG_OUTPUT=$(DEBUG_OUTPUT)

CC      = i686-elf-gcc
LD      = i686-elf-ld
NASM    = nasm
OBJCOPY = i686-elf-objcopy
READELF = i686-elf-readelf

CFLAGS  = -DNAME="__KERNEL__" -g -Wall -Wextra -O2 \
	-ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
	-fvisibility=hidden -mno-red-zone \
	-Werror-implicit-function-declaration -Wstrict-prototypes \
	-DDEBUG_OUTPUT=$(DEBUG_OUTPUT)

AFLAGS  = -f elf -g -F dwarf -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -I source/asm/

LDFLAGS_ELF = -T linker-elf.ld -static -nostdlib -no-pie --no-dynamic-linker --verbose -Map=bin/exos-elf.map

KERNEL_ELF = bin/exos.elf

SRC_C  = $(wildcard source/*.c) \
         $(wildcard source/test/*.c)
SRC_S  = $(wildcard source/asm/*.asm)

OBJ_C  = $(patsubst source/%.c, bin/%.o, $(SRC_C))
OBJ_S  = $(patsubst source/asm/%.asm, bin/%.o, $(SRC_S))

OBJS   = $(OBJ_S) $(OBJ_C)

# Sections to extract (edit as needed)
TEXT_BIN   = bin/text.bin
DATA_BIN   = bin/data.bin
BSS_BIN    = bin/bss.bin
FINAL_IMG  = bin/exos.bin

ALIGN      = 4096

.PHONY: all check_magic clean check-nogot

all: check_magic

$(KERNEL_ELF): $(OBJS) linker-elf.ld
	$(LD) $(LDFLAGS_ELF) -o $@ $(OBJS)

$(TEXT_BIN): $(KERNEL_ELF)
	@echo "Extracting .text section to $@"
	$(OBJCOPY) -O binary --only-section=.text $< $@

$(DATA_BIN): $(KERNEL_ELF)
	@echo "Extracting .data section to $@"
	$(OBJCOPY) -O binary --only-section=.data $< $@

$(BSS_BIN): $(KERNEL_ELF)
	@echo "Extracting .bss section to $@"
	$(OBJCOPY) -O binary --only-section=.bss $< $@

# Build final padded image
$(FINAL_IMG): $(TEXT_BIN) $(DATA_BIN) $(BSS_BIN)
	@echo "Building paged disk image: $@ (ALIGN=$(ALIGN))"

	# Clear binary file
	rm -f -- $@

	# Append .text
	cat $(TEXT_BIN) >> $@

	# Align current image to next ALIGN boundary (before .data)
	@{ \
		sz=$$(stat -c %s "$@"); \
		pad=$$(( ($(ALIGN) - (sz % $(ALIGN))) % $(ALIGN) )); \
		echo "Padding before .data: $$pad bytes"; \
		if [ $$pad -gt 0 ]; then \
			dd if=/dev/zero bs=1 count=$$pad >> "$@" 2>/dev/null; \
		fi; \
	}

	# Append .data
	cat $(DATA_BIN) >> $@

	# .bss is typically not written to disk (zero-initialized at runtime)

	# Append checksum
	@echo "Appending checksum to $(FINAL_IMG)"
	@cs=$$(head -c -0 -- "$(FINAL_IMG)" \
		| od -An -v -t u1 \
		| awk '{for(i=1;i<=NF;i++){s+=$$i; if(s>=4294967296)s-=4294967296}} END{printf "%u", s}'); \
	hex=$$(printf '%08x' $$cs); \
	b1=$${hex:0:2}; b2=$${hex:2:2}; b3=$${hex:4:2}; b4=$${hex:6:2}; \
	printf "\x$$b4\x$$b3\x$$b2\x$$b1" >> "$@"; \
	printf 'Checksum 0x%08X appended\n' $$cs

bin/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

bin/%.o: source/asm/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

check_magic: $(FINAL_IMG)
	@./check-exos-magic.sh $(FINAL_IMG)

# Optional sanity check to ensure the ELF has no GOT/PLT/DYNAMIC/INTERP sections
check-nogot: $(KERNEL_ELF)
	@echo "[check] scanning ELF for GOT/PLT/DYNAMIC/INTERP"
	@! $(READELF) -S $(KERNEL_ELF) 2>/dev/null | egrep -qi "(\\.(got(\\.|$$)|plt(\\.|$$)|dynamic|rela?)|interp)" || \
	( echo "ERROR: unexpected GOT/PLT/DYNAMIC/INTERP sections in $(KERNEL_ELF)"; \
	  $(READELF) -S $(KERNEL_ELF) | egrep -i "(\\.(got(\\.|$$)|plt(\\.|$$)|dynamic|rela?)|interp)"; exit 1 )

clean:
	rm -rf bin/*.o bin/*/*.o bin/*.elf bin/*.bin bin/*.map bin/*.img
