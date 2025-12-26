################################################################################
#
#       EXOS System Programs
#       Copyright (c) 1999-2025 Jango73
#
################################################################################

ARCH ?= i386

EXOS_MAKE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
EXOS_ROOT := $(abspath $(EXOS_MAKE_DIR)../..)
SYSTEM_ROOT := $(EXOS_ROOT)/system

APP_MAKEFILE := $(firstword $(MAKEFILE_LIST))
APP_DIR := $(abspath $(dir $(APP_MAKEFILE)))
REL_APP_DIR := $(patsubst $(SYSTEM_ROOT)/%,%,$(APP_DIR))
REL_APP_DIR := $(patsubst %/,%, $(REL_APP_DIR))

BUILD_DIR := $(EXOS_ROOT)/build/$(ARCH)
APP_OUT_DIR := $(BUILD_DIR)/system/$(REL_APP_DIR)

ifeq ($(strip $(APP_NAME)),)
$(error APP_NAME is not set)
endif

ifeq ($(strip $(APP_SOURCES)),)
$(error APP_SOURCES is not set)
endif

APP_HEADERS ?=

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

CFLAGS  = -ffreestanding -Wall -Wextra -O0 -fno-pic -fno-stack-protector -fno-builtin -fcf-protection=none \
          $(ARCH_CFLAGS)

LDFLAGS = -T $(EXOS_MAKE_DIR)exos.ld -nostdlib -Map=$(APP_OUT_DIR)/$(APP_NAME).map $(ARCH_LDFLAGS)

TARGET   = $(APP_OUT_DIR)/$(APP_NAME)
TARGET_SYMBOLS = $(APP_OUT_DIR)/$(APP_NAME).sym
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(APP_SOURCES)
OBJ_C  = $(patsubst source/%.c, $(APP_OUT_DIR)/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(TARGET) $(TARGET_SYMBOLS)

$(TARGET): $(OBJS) $(LIB_EXOS) $(EXOS_MAKE_DIR)exos.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIB_EXOS)

$(APP_OUT_DIR)/%.o: source/%.c $(APP_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET_SYMBOLS): $(TARGET)
	@echo "Extracting $(APP_NAME) symbols for Bochs debugging"
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(APP_OUT_DIR)/*.o $(TARGET) $(APP_OUT_DIR)/*.elf $(APP_OUT_DIR)/*.bin $(APP_OUT_DIR)/*.map $(TARGET_SYMBOLS)
