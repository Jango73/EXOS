ARCH ?= x86-32

ifneq ($(strip $(BUILD_CORE_NAME)),)
BUILD_DIR := ../build/core/$(BUILD_CORE_NAME)
else
BUILD_DIR := ../build/$(ARCH)
endif

CC = gcc
CFLAGS = -Wall -Wextra -O2
BINDIR = $(BUILD_DIR)/tools
SOURCEDIR = source

all: $(BINDIR)/cycle

$(BINDIR)/cycle: $(SOURCEDIR)/cycle.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(SOURCEDIR)/cycle.c

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
