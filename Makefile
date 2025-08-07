.PHONY: all kernel runtime portal boot-qemu boot-qemu-hd clean

all: kernel runtime portal boot-qemu-hd

kernel:
	@echo "[ Building kernel ]"
	@$(MAKE) -C kernel

runtime:
	@echo "[ Building runtime ]"
	@$(MAKE) -C runtime

portal:
	@echo "[ Building portal ]"
	@$(MAKE) -C portal

boot-qemu-hd:
	@echo "[ Building Qemu HD image ]"
	@$(MAKE) -C boot-qemu-hd

clean:
	@echo "[ Cleaning all ]"
	@$(MAKE) -C kernel clean
	@$(MAKE) -C runtime clean
	@$(MAKE) -C portal clean
	@$(MAKE) -C boot-qemu clean
	@$(MAKE) -C boot-freedos clean
	@$(MAKE) -C boot-qemu-hd clean
