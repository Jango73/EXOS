################################################################################
#
#	EXOS System Programs
#	Copyright (c) 1999-2025 Jango73
#
################################################################################

# Generic and compiler options
CC			= i686-elf-gcc
AS			= i686-elf-as
LD			= i686-elf-ld
OBJCOPY		= i686-elf-objcopy

CCOPT       = -DNAME="__KERNEL__" -g -Wall -Wextra -O0 -falign-functions=16 -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables -fvisibility=hidden -mno-red-zone -Werror-implicit-function-declaration -Wstrict-prototypes -DBARE_METAL=0 -DDEBUG_OUTPUT=0 -DSCHEDULING_DEBUG_OUTPUT=0 -DTRACE_STACK_USAGE=0

INCLUDES    = -I../runtime/include

################################################################################

.PHONY: all clean hello portal tictactoe netget master slave

all: hello portal tictactoe netget master slave

################################################################################
# Hello program

hello:
	@echo "[ Building hello ]"
	$(MAKE) -C hello all

hello_clean:
	$(MAKE) -C hello clean

################################################################################
# Portal program

portal:
	@echo "[ Building portal ]"
	$(MAKE) -C portal all

portal_clean:
	$(MAKE) -C portal clean

################################################################################
# TicTacToe program

tictactoe:
	@echo "[ Building tictactoe ]"
	$(MAKE) -C tictactoe all

tictactoe_clean:
	$(MAKE) -C tictactoe clean

################################################################################
# NetGet program

netget:
	@echo "[ Building netget ]"
	$(MAKE) -C netget all

netget_clean:
	$(MAKE) -C netget clean

################################################################################
# Master test program

master:
	@echo "[ Building master ]"
	$(MAKE) -C test/master all

master_clean:
	$(MAKE) -C test/master clean

################################################################################
# Slave test program

slave:
	@echo "[ Building slave ]"
	$(MAKE) -C test/slave all

slave_clean:
	$(MAKE) -C test/slave clean

################################################################################

clean: hello_clean portal_clean tictactoe_clean netget_clean master_clean slave_clean
	@echo "[ Cleaning system programs ]"
