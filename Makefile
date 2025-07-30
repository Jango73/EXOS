.PHONY: all kernel boot clean

all: kernel boot-qemu boot-freedos

kernel:
	@echo "==[ Building kernel ]=="
	@$(MAKE) -C kernel

boot-qemu:
	@echo "==[ Building simple boot image ]=="
	@$(MAKE) -C boot-qemu

boot-freedos:
	@echo "==[ Building freedos image ]=="
	@$(MAKE) -C boot-freedos

clean:
	@echo "==[ Cleaning all ]=="
	@$(MAKE) -C kernel clean
	@$(MAKE) -C boot-qemu clean
	@$(MAKE) -C boot-freedos clean
