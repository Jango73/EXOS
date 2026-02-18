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

all: $(BINDIR)/cycle

$(BINDIR)/cycle: $(CYCLE_SOURCE) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(CYCLE_SOURCE)

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
