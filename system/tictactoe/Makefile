BUILD_DIR = ../../build

CC      = i686-elf-gcc
LD      = i686-elf-ld

CFLAGS  = -ffreestanding -m32 -Wall -Wextra -O0 -g -fno-pic -fno-stack-protector -fno-builtin

LDFLAGS = -T tictactoe.ld -nostdlib -Map=$(BUILD_DIR)/system/tictactoe/tictactoe.map

TICTACTOE   = $(BUILD_DIR)/system/tictactoe/tictactoe
TICTACTOE_SYMBOLS = $(BUILD_DIR)/system/tictactoe/tictactoe.sym
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(wildcard source/*.c)
OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/system/tictactoe/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(TICTACTOE) $(TICTACTOE_SYMBOLS)

$(TICTACTOE): $(OBJS) $(LIB_EXOS) tictactoe.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIB_EXOS)

$(BUILD_DIR)/system/tictactoe/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TICTACTOE_SYMBOLS): $(TICTACTOE)
	@echo "Extracting tictactoe symbols for Bochs debugging"
	i686-elf-nm $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(BUILD_DIR)/system/tictactoe/*.o $(BUILD_DIR)/system/tictactoe/*.elf $(BUILD_DIR)/system/tictactoe/*.map $(TICTACTOE_SYMBOLS)
