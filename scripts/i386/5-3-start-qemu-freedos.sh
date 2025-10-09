cd boot-freedos
qemu-system-i386 -drive format=raw,file=bin/exos_dos.img -serial file:"../log/debug-com1.log" -serial file:"../log/kernel.log"
