hexdump -C ./kernel/bin/exos.bin | head -n 20
hexdump -C ./kernel/bin/exos.bin | tail -n 20
xxd -g1 -s +0x0098 -l 32 ./kernel/bin/exos.bin
readelf -S ./kernel/bin/exos.elf
nm -n ./kernel/bin/exos.elf | grep -E 'Start|stub|_start'
