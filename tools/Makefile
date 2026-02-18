ARCH ?= x86-32

ifeq ($(strip $(BUILD_CORE_NAME)),)
$(error BUILD_CORE_NAME is required)
endif
BUILD_DIR := ../build/core/$(BUILD_CORE_NAME)

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
