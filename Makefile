.PHONY: all kernel boot clean

all: kernel boot

kernel:
	@echo "==[ Building kernel ]=="
	@$(MAKE) -C kernel

boot:
	@echo "==[ Building boot image ]=="
	@$(MAKE) -C boot-qemu

clean:
	@echo "==[ Cleaning all ]=="
	@$(MAKE) -C kernel clean
	@$(MAKE) -C boot-qemu clean
