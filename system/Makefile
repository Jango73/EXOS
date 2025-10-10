################################################################################
#
#       EXOS System Programs
#       Copyright (c) 1999-2025 Jango73
#
################################################################################

# Architecture selection (default to i386)
ARCH ?= i386

ifeq ($(ARCH),i386)
# Using i386 cross-toolchain
else ifeq ($(ARCH),x86-64)
# Using host toolchain for x86-64 builds
else
$(error Unsupported architecture $(ARCH))
endif

SUBMAKE = $(MAKE) ARCH=$(ARCH)

################################################################################

.PHONY: all clean hello portal tictactoe netget master slave

all: hello portal tictactoe netget master slave

################################################################################
# Hello program

hello:
	@echo "[ Building hello ]"
	$(SUBMAKE) -C hello all

hello_clean:
	$(SUBMAKE) -C hello clean

################################################################################
# Portal program

portal:
	@echo "[ Building portal ]"
	$(SUBMAKE) -C portal all

portal_clean:
	$(SUBMAKE) -C portal clean

################################################################################
# TicTacToe program

tictactoe:
	@echo "[ Building tictactoe ]"
	$(SUBMAKE) -C tictactoe all

tictactoe_clean:
	$(SUBMAKE) -C tictactoe clean

################################################################################
# NetGet program

netget:
	@echo "[ Building netget ]"
	$(SUBMAKE) -C netget all

netget_clean:
	$(SUBMAKE) -C netget clean

################################################################################
# Master test program

master:
	@echo "[ Building master ]"
	$(SUBMAKE) -C test/master all

master_clean:
	$(SUBMAKE) -C test/master clean

################################################################################
# Slave test program

slave:
	@echo "[ Building slave ]"
	$(SUBMAKE) -C test/slave all

slave_clean:
	$(SUBMAKE) -C test/slave clean

################################################################################

clean: hello_clean portal_clean tictactoe_clean netget_clean master_clean slave_clean
	@echo "[ Cleaning system programs ]"
