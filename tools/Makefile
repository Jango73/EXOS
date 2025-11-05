ARCH ?= i386

BUILD_DIR := ../build/$(ARCH)

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
