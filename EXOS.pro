# EXOS kernel - QtCreator dummy project (no build, code-model only)
# Purpose: index & browse sources; attach GDB to QEMU separately.

TEMPLATE = aux
CONFIG += no_keywords

# --- Project root (Windows checkout path) ---
EXOS_ROOT = $$PWD

# --- Code model includes/defines (adjust if you want) ---
INCLUDEPATH += $$EXOS_ROOT/kernel/source $$EXOS_ROOT/kernel
DEFINES += EXOS_KERNEL=1

# --- Collect sources/headers recursively for indexing ---
SOURCES += \
    $$files($$EXOS_ROOT/kernel/*.c, true) \
    $$files($$EXOS_ROOT/kernel/source/*.c, true)

HEADERS += \
    $$files($$EXOS_ROOT/kernel/*.h, true) \
    $$files($$EXOS_ROOT/kernel/source/*.h, true)

# --- Show ASM files in the project tree (not built) ---
OTHER_FILES += \
    $$files($$EXOS_ROOT/kernel/source/asm/*.asm, true)

# --- Disable any accidental build/link (safe no-op toolchain) ---
QMAKE_CC = echo
QMAKE_CXX = echo
QMAKE_LINK = echo
QMAKE_AR = echo
QMAKE_RANLIB = echo
QMAKE_CFLAGS +=
QMAKE_CXXFLAGS +=
