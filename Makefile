.PHONY: all kernel runtime hello portal boot-qemu boot-qemu-hd clean

all: kernel runtime hello portal boot-qemu-hd

kernel:
	@echo "[ Building kernel ]"
	@$(MAKE) -C kernel

runtime:
	@echo "[ Building runtime ]"
	@$(MAKE) -C runtime

hello:
	@echo "[ Building hello ]"
	@$(MAKE) -C hello

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
	@$(MAKE) -C hello clean
	@$(MAKE) -C portal clean
	@$(MAKE) -C boot-freedos clean
	@$(MAKE) -C boot-qemu-hd clean
