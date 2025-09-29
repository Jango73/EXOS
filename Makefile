.PHONY: all kernel runtime system boot-qemu boot-hd tools clean

all: log kernel runtime system boot-hd tools

log:
	@mkdir -p log

kernel:
	@echo "[ Building kernel ]"
	@$(MAKE) -C kernel

runtime:
	@echo "[ Building runtime ]"
	@$(MAKE) -C runtime

system: runtime
	@echo "[ Building system programs ]"
	@$(MAKE) -C system

boot-hd: kernel system
	@echo "[ Building Qemu HD image ]"
	@$(MAKE) -C boot-hd

tools:
	@echo "[ Building tools ]"
	@$(MAKE) -C tools

clean:
	@echo "[ Cleaning all ]"
	@$(MAKE) -C kernel clean
	@$(MAKE) -C runtime clean
	@$(MAKE) -C system clean
	@$(MAKE) -C boot-freedos clean
	@$(MAKE) -C boot-hd clean
	@$(MAKE) -C tools clean
