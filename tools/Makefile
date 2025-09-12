CC = gcc
CFLAGS = -Wall -Wextra -O2
BINDIR = bin

all: $(BINDIR)/cycle

$(BINDIR)/cycle: cycle.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ cycle.c

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
