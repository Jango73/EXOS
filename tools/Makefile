ARCH ?= x86-32

ifeq ($(strip $(BUILD_CORE_NAME)),)
$(error BUILD_CORE_NAME is required)
endif
BUILD_DIR := ../build/core/$(BUILD_CORE_NAME)

CC = gcc
CFLAGS = -Wall -Wextra -O2
BINDIR = $(BUILD_DIR)/tools
SOURCEDIR = source
CYCLE_SOURCE = $(SOURCEDIR)/core/cycle.c
EPK_PACK_SOURCE = $(SOURCEDIR)/package/epk_pack.c
EPK_PACK_SOURCES = \
	$(EPK_PACK_SOURCE) \
	../third/miniz/miniz.c \
	../third/miniz/miniz_tdef.c \
	../third/miniz/miniz_tinfl.c \
	../third/bearssl/src/hash/sha2small.c \
	../third/bearssl/src/codec/dec32be.c \
	../third/bearssl/src/codec/enc32be.c
EPK_PACK_CFLAGS = $(CFLAGS) -I../third/miniz -I../third/bearssl/inc -I../third/bearssl/src

all: $(BINDIR)/cycle $(BINDIR)/epk-pack

$(BINDIR)/cycle: $(CYCLE_SOURCE) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(CYCLE_SOURCE)

$(BINDIR)/epk-pack: $(EPK_PACK_SOURCES) | $(BINDIR)
	$(CC) $(EPK_PACK_CFLAGS) -o $@ $(EPK_PACK_SOURCES)

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
