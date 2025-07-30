# Makefile mtools - injecte EXOS dans une image FreeDOS avec partition FAT32

LOADER_SRC = source/loader.asm
LOADER_COM = bin/loader.com
KERNEL_BIN = ../kernel/bin/exos.bin
BASE_IMG   = freedos/FD14FULL.img
FINAL_IMG  = bin/exos_dos.img
AUTOEXEC   = bin/AUTOEXEC.BAT

# Offset de la première partition FAT32 dans FD14FULL.img (en octets)
# 2048 secteurs * 512 = 1048576
MTOOLS_CONF = .mtoolsrc.tmp

PART_OFFSET := $(shell parted -s bin/exos_dos.img unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }')

all: check_mtools $(FINAL_IMG)

$(FINAL_IMG): $(LOADER_COM) $(KERNEL_BIN)
	@echo ">>> PART_OFFSET = $(PART_OFFSET)"

	@echo ">>> Création d'AUTOEXEC.BAT..."
	echo "loader.com" > $(AUTOEXEC)

	@echo ">>> Génération du fichier mtools temporaire..."
	echo "drive z: file=\"$(FINAL_IMG)\" offset=$(PART_OFFSET)" > $(MTOOLS_CONF)

	@echo ">>> Injection via mtools avec offset ($(PART_OFFSET))..."
	MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(LOADER_COM) z:LOADER.COM
	MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(KERNEL_BIN)  z:EXOS.BIN
	MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(AUTOEXEC)    z:AUTOEXEC.BAT

	@echo ">>> Image prête: $(FINAL_IMG)"

	@rm -f $(MTOOLS_CONF)

$(LOADER_COM): $(LOADER_SRC)
	@echo ">>> Assemblage du loader..."
	nasm -f bin $(LOADER_SRC) -o $(LOADER_COM)

check_mtools:
	@command -v mcopy >/dev/null 2>&1 || { echo "❌ mtools n'est pas installé. Fais: sudo apt install mtools" ; exit 1; }

clean:
	rm -f $(LOADER_COM) $(AUTOEXEC) $(MTOOLS_CONF)
