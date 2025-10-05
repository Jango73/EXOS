
BUILD_DIR = ../build

CC      = i686-elf-gcc
CXX     = i686-elf-g++
LD      = i686-elf-ld
AS      = AS = i686-elf-as
NASM    = nasm

CFLAGS  = -g -Wall -Wextra -O0 \
	-ffreestanding -fno-stack-protector -mno-stack-arg-probe -fno-builtin -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
	-fvisibility=hidden -mno-red-zone \
	-Werror-implicit-function-declaration -Wstrict-prototypes \
	-I../kernel/include

CXXFLAGS = -g -Wall -Wextra -O0 -std=c++11 \
	-ffreestanding -fno-stack-protector -mno-stack-arg-probe -fno-builtin -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
	-fvisibility=hidden -mno-red-zone \
	-fno-rtti -fno-threadsafe-statics

AFLAGS  = -f elf -g -F dwarf -I source/

LDFLAGS = -T linker.ld -nostdlib -Map=$(BUILD_DIR)/runtime/exos-runtime.map

RUNTIME_LIB = $(BUILD_DIR)/runtime/libexos.a

SRC_C   = $(wildcard source/*.c) ../kernel/source/String.c ../kernel/source/utils/AdaptiveDelay.c
SRC_CXX = $(wildcard source/*.cpp)
SRC_S   = $(wildcard source/*.asm)

OBJ_C   = $(patsubst source/%.c, $(BUILD_DIR)/runtime/%.o, $(filter source/%.c, $(SRC_C))) $(patsubst ../kernel/source/%.c, $(BUILD_DIR)/runtime/%.o, $(filter ../kernel/source/%.c, $(SRC_C)))
# OBJ_CXX = $(patsubst source/%.cpp, $(BUILD_DIR)/runtime/%.o, $(SRC_CXX))
OBJ_S   = $(patsubst source/%.asm, $(BUILD_DIR)/runtime/%.o, $(SRC_S))

OBJS    = $(OBJ_S) $(OBJ_C) # $(OBJ_CXX)

# === Build rules ===

all: $(RUNTIME_LIB)

$(RUNTIME_LIB): $(OBJS)
	@mkdir -p $(dir $@)
	i686-elf-ar rcs $@ $^

$(BUILD_DIR)/runtime/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/runtime/%.o: source/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/runtime/%.o: ../kernel/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/runtime/%.o: source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)/runtime/*.o $(BUILD_DIR)/runtime/*.elf $(BUILD_DIR)/runtime/*.bin $(BUILD_DIR)/runtime/*.a $(BUILD_DIR)/runtime/*.map
