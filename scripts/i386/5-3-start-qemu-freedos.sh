cd boot-freedos
IMG_1_PATH="../build/i386/boot-freedos/exos_dos.img"
qemu-system-i386 -drive format=raw,file="$IMG_1_PATH" -serial file:"../log/debug-com1.log" -serial file:"../log/kernel.log"
