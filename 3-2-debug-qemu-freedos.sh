cd boot-freedos
qemu-system-i386 -drive format=raw,file=bin/exos_dos.img -s -S &
sleep 1
cgdb ../kernel/bin/exos.elf -ex "target remote localhost:1234" -ex "break ProtectedModeEntry"
