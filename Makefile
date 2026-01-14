include make/arch.mk

USE_SYSCALL ?= 0
PROFILING ?= 0

SUBMAKE = $(MAKE) ARCH=$(ARCH) VMA_KERNEL=$(VMA_KERNEL) USE_SYSCALL=$(USE_SYSCALL) PROFILING=$(PROFILING)

.PHONY: all kernel runtime system boot-qemu boot-hd boot-uefi tools clean

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

boot-uefi: kernel
	@echo "[ Building UEFI image ]"
	@+$(SUBMAKE) -C boot-uefi

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
	@+$(SUBMAKE) -C boot-uefi clean
	@+$(SUBMAKE) -C tools clean
