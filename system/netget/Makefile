ARCH ?= i386

BUILD_DIR := ../../build/$(ARCH)

ifeq ($(ARCH),i386)
TOOLCHAIN_PREFIX = i686-elf
CC      = $(TOOLCHAIN_PREFIX)-gcc
LD      = $(TOOLCHAIN_PREFIX)-ld
NM      = $(TOOLCHAIN_PREFIX)-nm
ARCH_CFLAGS =
ARCH_LDFLAGS =
else ifeq ($(ARCH),x86-64)
CC      = gcc
LD      = ld
NM      = nm
ARCH_CFLAGS = -m64
ARCH_LDFLAGS = -m elf_x86_64
else
$(error Unsupported architecture $(ARCH))
endif

CFLAGS  = -ffreestanding -Wall -Wextra -O0 -g -fno-pic -fno-stack-protector -fno-builtin -fcf-protection=none \
          $(ARCH_CFLAGS)

LDFLAGS = -T netget.ld -nostdlib -Map=$(BUILD_DIR)/system/netget/netget.map $(ARCH_LDFLAGS)

NETGET   = $(BUILD_DIR)/system/netget/netget
NETGET_SYMBOLS = $(BUILD_DIR)/system/netget/netget.sym
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/system/netget/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(NETGET) $(NETGET_SYMBOLS)

$(NETGET): $(OBJS) $(LIB_EXOS) netget.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIB_EXOS)

$(BUILD_DIR)/system/netget/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(NETGET_SYMBOLS): $(NETGET)
	@echo "Extracting netget symbols for Bochs debugging"
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(BUILD_DIR)/system/netget/*.o $(BUILD_DIR)/system/netget/netget $(BUILD_DIR)/system/netget/*.elf $(BUILD_DIR)/system/netget/*.map $(NETGET_SYMBOLS)
