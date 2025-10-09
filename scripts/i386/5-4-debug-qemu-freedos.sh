cd boot-freedos
qemu-system-i386 -drive format=raw,file=bin/exos_dos.img -serial file:"../log/debug-com1.log" -serial file:"../log/kernel.log" -s -S &
sleep 1
cgdb-multiarch ../kernel/bin/exos.elf -ex "target remote localhost:1234" -ex "break Kernel.c:584"
