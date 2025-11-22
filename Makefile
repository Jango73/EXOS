ARCH ?= i386
USE_SYSCALL ?= 0
PROFILING ?= 0

ifeq ($(ARCH),i386)
VMA_KERNEL ?= 0xC0000000
else ifeq ($(ARCH),x86-64)
VMA_KERNEL ?= 0xFFFFFFFFC0000000
endif

SUBMAKE = $(MAKE) ARCH=$(ARCH) VMA_KERNEL=$(VMA_KERNEL) USE_SYSCALL=$(USE_SYSCALL) PROFILING=$(PROFILING)

.PHONY: all kernel runtime system boot-qemu boot-hd tools clean

all: log kernel runtime system boot-hd tools

log:
	@mkdir -p log

kernel:
	@echo "[ Building kernel ]"
	@+$(SUBMAKE) -C kernel

runtime:
	@echo "[ Building runtime ]"
	@+$(SUBMAKE) -C runtime

system: runtime
	@echo "[ Building system programs ]"
	@+$(SUBMAKE) -C system

boot-hd: kernel system
	@echo "[ Building Qemu HD image ]"
	@+$(SUBMAKE) -C boot-hd

tools:
	@echo "[ Building tools ]"
	@+$(SUBMAKE) -C tools

clean:
	@echo "[ Cleaning all ]"
	@+$(SUBMAKE) -C kernel clean
	@+$(SUBMAKE) -C runtime clean
	@+$(SUBMAKE) -C system clean
	@+$(SUBMAKE) -C boot-freedos clean
	@+$(SUBMAKE) -C boot-hd clean
	@+$(SUBMAKE) -C tools clean
