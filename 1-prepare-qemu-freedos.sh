cd boot-freedos

@echo "[Downloading FD13-LiveCD.zip]"
wget https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/previews/1.3-rc4/FD13-LiveCD.zip -O freedos/FD13-LiveCD.zip
unzip freedos/FD13-LiveCD.zip -d freedos/

@echo "[Creating DOS image]"
qemu-img create -f raw bin/exos_dos.img 25M

@echo "[Launching DOS install]"
qemu-system-i386 -drive format=raw,file=bin/exos_dos.img -cdrom freedos/FD13LIVE.iso -boot d
