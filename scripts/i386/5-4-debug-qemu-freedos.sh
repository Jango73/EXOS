cd boot-freedos
IMG_1_PATH="../build/i386/boot-freedos/exos_dos.img"
qemu-system-i386 -drive format=raw,file="$IMG_1_PATH" -serial file:"../log/debug-com1.log" -serial file:"../log/kernel.log" -s -S &
sleep 1
cgdb-multiarch ../build/i386/kernel/exos.elf -ex "target remote localhost:1234" -ex "break Kernel.c:584"
