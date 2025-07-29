
# ---- Compilers, linkers ----

NAME  = exos
CCOMP = gcc
ACOMP = nasm
CFLAG = -c -funsigned-bitfields -fwritable-strings -o $@
AFLAG = -f coff -i src -o $@
LINK  = jloc

c_obj =              \
  bin/main.o         \
  bin/kernel.o       \
  bin/log.o          \
  bin/text.o         \
  bin/string.o       \
  bin/list.o         \
  bin/heap.o         \
  bin/fault.o        \
  bin/crypt.o        \
  bin/clock.o        \
  bin/schedule.o     \
  bin/segment.o      \
  bin/vmm.o          \
  bin/process.o      \
  bin/task.o         \
  bin/sem.o          \
  bin/desktop.o      \
  bin/syscall.o      \
  bin/drvcall.o      \
  bin/file.o         \
  bin/filesys.o      \
  bin/systemfs.o     \
  bin/fat16.o        \
  bin/fat32.o        \
  bin/xfs.o          \
  bin/keyboard.o     \
  bin/key.o          \
  bin/sermouse.o     \
  bin/console.o      \
  bin/vesa.o         \
  bin/hd.o           \
  bin/ramdisk.o      \
  bin/dco.o          \
  bin/shell.o        \
  bin/memedit.o      \
  bin/edit.o

a_obj =              \
  bin/stub.o        \
  bin/system.o       \
  bin/rmc.o          \
  bin/int.o

headers =            \
  src/address.h      \
  src/base.h         \
  src/clock.h        \
  src/console.h      \
  src/crypt.h        \
  src/data.h         \
  src/driver.h       \
  src/fat.h          \
  src/file.h         \
  src/filesys.h      \
  src/fsid.h         \
  src/gfx.h          \
  src/hd.h           \
  src/heap.h         \
  src/i386.h         \
  src/id.h           \
  src/kernel.h       \
  src/keyboard.h     \
  src/list.h         \
  src/mouse.h        \
  src/process.h      \
  src/string.h       \
  src/system.h       \
  src/text.h         \
  src/user.h         \
  src/vararg.h       \
  src/vkey.h         \
  src/vmm.h          \
  src/xfs.h

# ---- Executable creation ----

$(NAME).bin : all_obj
	$(LINK) $(NAME).lnk bin/$(NAME).bin map/$(NAME).map

#$(NAME).bin : $(c_obj) $(a_obj)
#  $(LINK) $(NAME).lnk bin/$(NAME).bin map/$(NAME).map

# ---- Objects ----

all_obj : c_obj a_obj

c_obj : $(c_obj) $(headers)

a_obj : $(a_obj)

$(c_obj) : bin/%.o : src/%.c
	$(CCOMP) $(CFLAG) $<

$(a_obj) : bin/%.o : src/%.asm
	$(ACOMP) $(AFLAG) $<
