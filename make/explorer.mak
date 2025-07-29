
# ---- Compilers, linkers ----

NAME  = explorer
CCOMP = gcc
ACOMP = nasm
LINK  = jloc

# ---- Objects ----

.c.o: $(CCOMP) $*.o $<

.asm.o: $(ACOMP) -f coff $*.o $<

.asm.obj: $(ACOMP) -f obj $*.obj $<

objects = explorer.o exos.o exosrt1.o exosrt2.o

# ---- Executable creation ----

$(NAME).bin: $(objects)
	$(LINK) $(NAME).lnk $(NAME).bin $(NAME).map
