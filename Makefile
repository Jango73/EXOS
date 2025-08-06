.PHONY: all kernel runtime portal boot-qemu boot-limine clean

all: kernel runtime portal boot-qemu boot-limine

kernel:
	@echo "[ Building kernel ]"
	@$(MAKE) -C kernel

runtime:
	@echo "[ Building runtime ]"
	@$(MAKE) -C runtime

portal:
	@echo "[ Building portal ]"
	@$(MAKE) -C portal

boot-limine:
	@echo "[ Building limine image ]"
	@$(MAKE) -C boot-limine

clean:
	@echo "[ Cleaning all ]"
	@$(MAKE) -C kernel clean
	@$(MAKE) -C runtime clean
	@$(MAKE) -C portal clean
	@$(MAKE) -C boot-qemu clean
	@$(MAKE) -C boot-freedos clean
	@$(MAKE) -C boot-limine clean
