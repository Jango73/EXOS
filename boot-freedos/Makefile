# Makefile mtools - inject EXOS into a FreeDOS image with FAT32 partition

LOADER_SRC     = source/loader.asm
LOADER_COM     = bin/loader.com
KERNEL_BIN     = ../kernel/bin/exos.bin
BASE_IMG       = freedos/FD14FULL.img
FINAL_IMG      = bin/exos_dos.img
AUTOEXEC       = bin/AUTOEXEC.BAT
FD_AUTO_TMP    = ./FDAUTO_TMP.BAT
MTOOLS_CONF    = .mtoolsrc.tmp

# Offset of the first FAT32 partition in FD14FULL.img (in bytes)
PART_OFFSET := $(shell parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }')

all: check_mtools $(FINAL_IMG) update_fdauto

$(FINAL_IMG): $(LOADER_COM) $(KERNEL_BIN)
	@echo ">>> PART_OFFSET = $(PART_OFFSET)"

	@echo ">>> Creating AUTOEXEC.BAT..."
	echo "ECHO Launching Loader\r\nLOADER.COM" > $(AUTOEXEC)

	@echo ">>> Generating temporary mtools config file..."
	echo "drive z: file=\"$(FINAL_IMG)\" offset=$(PART_OFFSET)" > $(MTOOLS_CONF)

	@echo ">>> Injecting files into image (offset: $(PART_OFFSET))..."
	MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(AUTOEXEC)   z:AUTOEXEC.BAT
	MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(LOADER_COM) z:LOADER.COM
	MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(KERNEL_BIN) z:EXOS.BIN

	@echo ">>> Image ready: $(FINAL_IMG)"

	@rm -f $(MTOOLS_CONF)

update_fdauto:
	@echo ">>> Generating temporary mtools config file for FDAUTO.BAT update..."
	echo "drive z: file=\"$(FINAL_IMG)\" offset=$(PART_OFFSET)" > $(MTOOLS_CONF)

	@echo ">>> Extracting FDAUTO.BAT from FINAL_IMG..."
	@if MTOOLSRC=$(MTOOLS_CONF) mcopy -o z:FDAUTO.BAT $(FD_AUTO_TMP); then \
		echo ">>> FDAUTO.BAT extracted successfully."; \
	else \
		echo ">>> FDAUTO.BAT not found, creating empty file."; \
		echo "" > $(FD_AUTO_TMP); \
	fi

	@echo ">>> Checking/inserting LOADER.COM in FDAUTO.BAT..."
	@if ! grep -i '^[ \t]*\(call[ \t]\+\)\?LOADER\.COM' $(FD_AUTO_TMP) > /dev/null; then \
		echo "LOADER.COM" >> $(FD_AUTO_TMP); \
		echo ">>> LOADER.COM injected into FDAUTO.BAT."; \
		echo ">>> Writing updated FDAUTO.BAT to FINAL_IMG..."; \
		MTOOLSRC=$(MTOOLS_CONF) mcopy -o $(FD_AUTO_TMP) z:FDAUTO.BAT; \
	else \
		echo ">>> LOADER.COM already present, nothing to do."; \
	fi

	@echo ">>> Cleaning up temporary files..."
	@rm -f $(FD_AUTO_TMP) $(MTOOLS_CONF)

$(LOADER_COM): $(LOADER_SRC)
	@echo ">>> Assembling loader..."
	nasm -f bin $(LOADER_SRC) -o $(LOADER_COM)

check_mtools:
	@command -v mcopy >/dev/null 2>&1 || { echo "❌ mtools is not installed. Please run: sudo apt install mtools" ; exit 1; }

clean:
	rm -f $(LOADER_COM) $(AUTOEXEC) $(MTOOLS_CONF) $(FD_AUTO_TMP)
