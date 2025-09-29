BUILD_DIR = ../../build

CC      = i686-elf-gcc
LD      = i686-elf-ld

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O0 -g -fno-pic -fno-stack-protector -fno-builtin

LDFLAGS = -T netget.ld -nostdlib -Map=$(BUILD_DIR)/system/netget/netget.map

NETGET   = $(BUILD_DIR)/system/netget/netget
NETGET_SYMBOLS = $(BUILD_DIR)/system/netget/netget.sym
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/system/netget/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(NETGET) $(NETGET_SYMBOLS)

$(NETGET): $(OBJS) $(LIB_EXOS) netget.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIB_EXOS)

$(BUILD_DIR)/system/netget/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(NETGET_SYMBOLS): $(NETGET)
	@echo "Extracting netget symbols for Bochs debugging"
	i686-elf-nm $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(BUILD_DIR)/system/netget/*.o $(BUILD_DIR)/system/netget/*.elf $(BUILD_DIR)/system/netget/*.map $(NETGET_SYMBOLS)
