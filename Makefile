ARCH ?= i386

SUBMAKE = $(MAKE) ARCH=$(ARCH)

.PHONY: all kernel runtime system boot-qemu boot-hd tools clean

all: log kernel runtime system boot-hd tools

log:
	@mkdir -p log

kernel:
	@echo "[ Building kernel ]"
	@$(SUBMAKE) -C kernel

runtime:
	@echo "[ Building runtime ]"
	@$(SUBMAKE) -C runtime

system: runtime
	@echo "[ Building system programs ]"
	@$(SUBMAKE) -C system

boot-hd: kernel system
	@echo "[ Building Qemu HD image ]"
	@$(SUBMAKE) -C boot-hd

tools:
	@echo "[ Building tools ]"
	@$(SUBMAKE) -C tools

clean:
	@echo "[ Cleaning all ]"
	@$(SUBMAKE) -C kernel clean
	@$(SUBMAKE) -C runtime clean
	@$(SUBMAKE) -C system clean
	@$(SUBMAKE) -C boot-freedos clean
	@$(SUBMAKE) -C boot-hd clean
	@$(SUBMAKE) -C tools clean
