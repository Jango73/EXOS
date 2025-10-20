
SHELL = /bin/bash

ARCH ?= i386

BUILD_DIR := ../build/$(ARCH)

ARCH_CFLAGS =

ifeq ($(ARCH),i386)
TOOLCHAIN_PREFIX = i686-elf
NASM_FORMAT      = elf
CC               = $(TOOLCHAIN_PREFIX)-gcc
LD               = $(TOOLCHAIN_PREFIX)-ld
NM               = $(TOOLCHAIN_PREFIX)-nm
OBJCOPY          = $(TOOLCHAIN_PREFIX)-objcopy
READELF          = $(TOOLCHAIN_PREFIX)-readelf
else ifeq ($(ARCH),x86-64)
NASM_FORMAT      = elf64
CC               = gcc
LD               = ld
NM               = nm
OBJCOPY          = objcopy
READELF          = readelf
ARCH_CFLAGS      = -m64 -mcmodel=large
else
$(error Unsupported architecture $(ARCH))
endif

BARE_METAL              ?= 0
DEBUG_OUTPUT            ?= 0
SCHEDULING_DEBUG_OUTPUT ?= 0
TRACE_STACK_USAGE       ?= 0

NASMFLAGS       = -DDEBUG_OUTPUT=$(DEBUG_OUTPUT)
CFLAGS          = -D__KERNEL__ -g -Wall -Wextra -O0 -falign-functions=16 \
                  -ffreestanding -fno-builtin -nostdinc \
                  -fno-pic -fno-pie -fno-stack-protector \
                  -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
                  -fvisibility=hidden -mno-red-zone \
                  -Werror-implicit-function-declaration -Wstrict-prototypes \
                  -DBARE_METAL=$(BARE_METAL) -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -DSCHEDULING_DEBUG_OUTPUT=$(SCHEDULING_DEBUG_OUTPUT) -DTRACE_STACK_USAGE=$(TRACE_STACK_USAGE) \
                  -Iinclude -I../third/include -I../runtime/include
CFLAGS         += $(ARCH_CFLAGS)

NASM            = nasm

AFLAGS_INCLUDES = -I source/asm/ -I source/arch/$(ARCH)/asm/

ifeq ($(ARCH),x86-64)
AFLAGS_INCLUDES += -I source/arch/i386/asm/
endif

AFLAGS          = -f $(NASM_FORMAT) -g -F dwarf -d__KERNEL__ -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -DSCHEDULING_DEBUG_OUTPUT=$(SCHEDULING_DEBUG_OUTPUT) $(AFLAGS_INCLUDES)

LINKER_SCRIPT   = exos-$(ARCH).ld

LDFLAGS_ELF     = -T $(LINKER_SCRIPT) -static -nostdlib -no-pie --no-dynamic-linker -Map=$(BUILD_DIR)/kernel/exos.map

KERNEL_ELF      = $(BUILD_DIR)/kernel/exos.elf

SRC_C  = $(wildcard source/*.c) \
         $(wildcard source/arch/intel/*.c) \
         $(wildcard source/arch/$(ARCH)/*.c) \
         $(wildcard source/network/*.c) \
         $(wildcard source/drivers/*.c) \
         $(wildcard source/utils/*.c) \
         $(wildcard source/test/*.c) \
         $(wildcard source/autotest/*.c) \
         ../third/bcrypt/blowfish.c \
         ../third/bcrypt/wrapbf.c \
         ../third/bcrypt/endian.c \
         ../runtime/source/exos-runtime-c.c
SRC_S  = $(wildcard source/asm/*.asm) \
         $(wildcard source/arch/$(ARCH)/asm/*.asm)

ifeq ($(ARCH),x86-64)
SRC_S += ../runtime/source/x86-64/exos-runtime-a.asm
else
SRC_S += ../runtime/source/i386/exos-runtime-a.asm
endif

OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/kernel/%.o, $(filter source/%.c, $(SRC_C))) \
         $(patsubst ../third/bcrypt/%.c, $(BUILD_DIR)/kernel/bcrypt_%.o, $(filter ../third/bcrypt/%.c, $(SRC_C))) \
         $(patsubst ../runtime/source/%.c, $(BUILD_DIR)/kernel/runtime_%.o, $(filter ../runtime/source/%.c, $(SRC_C)))
OBJ_S  = $(patsubst source/%.asm, $(BUILD_DIR)/kernel/%.o, $(filter source/%.asm, $(SRC_S))) \
         $(patsubst ../runtime/source/%.asm, $(BUILD_DIR)/kernel/runtime_%.o, $(filter ../runtime/source/%.asm, $(SRC_S)))

OBJS   = $(OBJ_S) $(OBJ_C)

# Sections to extract (edit as needed)
TEXT_BIN        = $(BUILD_DIR)/kernel/text.bin
SHARED_TEXT_BIN = $(BUILD_DIR)/kernel/shared_text.bin
DATA_BIN   		= $(BUILD_DIR)/kernel/data.bin
BSS_BIN    		= $(BUILD_DIR)/kernel/bss.bin
FINAL_IMG  		= $(BUILD_DIR)/kernel/exos.bin

ALIGN      		= 4096

.PHONY: all check_magic clean check-nogot

all: check_magic $(BUILD_DIR)/kernel/exos.sym

$(KERNEL_ELF): $(OBJS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS_ELF) -o $@ $(OBJS)

$(TEXT_BIN): $(KERNEL_ELF)
	@echo "Extracting .text section to $@"
	$(OBJCOPY) -O binary --only-section=.text $< $@

$(SHARED_TEXT_BIN): $(KERNEL_ELF)
	@echo "Extracting .shared_text section to $@"
	$(OBJCOPY) -O binary --only-section=.shared_text $< $@

$(DATA_BIN): $(KERNEL_ELF)
	@echo "Extracting .data section to $@"
	$(OBJCOPY) -O binary --only-section=.data $< $@

$(BSS_BIN): $(KERNEL_ELF)
	@echo "Extracting .bss section to $@"
	$(OBJCOPY) -O binary --only-section=.bss $< $@

# Build final padded image
$(FINAL_IMG): $(TEXT_BIN) $(SHARED_TEXT_BIN) $(DATA_BIN) $(BSS_BIN)
	@echo "Building paged disk image: $@ (ALIGN=$(ALIGN))"

	# Clear binary file
	rm -f -- $@

	# Append .text
	cat $(TEXT_BIN) >> $@

	# Align current image to next ALIGN boundary (before .shared_text)
	@{ \
		sz=$$(stat -c %s "$@"); \
		pad=$$(( ($(ALIGN) - (sz % $(ALIGN))) % $(ALIGN) )); \
		echo "Padding before .shared_text: $$pad bytes"; \
		if [ $$pad -gt 0 ]; then \
			dd if=/dev/zero bs=1 count=$$pad >> "$@" 2>/dev/null; \
		fi; \
	}

	# Append .data
	cat $(SHARED_TEXT_BIN) >> $@

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

$(BUILD_DIR)/kernel/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/%.o: source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

$(BUILD_DIR)/kernel/bcrypt_%.o: ../third/bcrypt/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/runtime_%.o: ../runtime/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/runtime_%.o: ../runtime/source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

check_magic: $(FINAL_IMG)
	@./check-exos-magic.sh $(FINAL_IMG)

# Extract debug symbols for Bochs
$(BUILD_DIR)/kernel/exos.sym: $(KERNEL_ELF)
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

# Optional sanity check to ensure the ELF has no GOT/PLT/DYNAMIC/INTERP sections
check-nogot: $(KERNEL_ELF)
	@echo "[check] scanning ELF for GOT/PLT/DYNAMIC/INTERP"
	@! $(READELF) -S $(KERNEL_ELF) 2>/dev/null | egrep -qi "(\\.(got(\\.|$$)|plt(\\.|$$)|dynamic|rela?)|interp)" || \
	( echo "ERROR: unexpected GOT/PLT/DYNAMIC/INTERP sections in $(KERNEL_ELF)"; \
	  $(READELF) -S $(KERNEL_ELF) | egrep -i "(\\.(got(\\.|$$)|plt(\\.|$$)|dynamic|rela?)|interp)"; exit 1 )

clean:
	rm -rf $(BUILD_DIR)/kernel/*.o $(BUILD_DIR)/kernel/*/*.o $(BUILD_DIR)/kernel/*.elf $(BUILD_DIR)/kernel/*.bin $(BUILD_DIR)/kernel/*.map $(BUILD_DIR)/kernel/*.img $(BUILD_DIR)/kernel/*.sym
