#!/bin/bash

set -e
cd boot-limine

# Config
LIMINE_DIR="./limine"
LIMINE_SYS="$LIMINE_DIR/bin/limine-bios.sys"

# Prerequisites check
for cmd in git make autoconf automake; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing command: $cmd"
        exit 1
    fi
done

# Clone Limine
if [ ! -d "$LIMINE_DIR" ]; then
    echo "Cloning Limine..."
    git clone https://github.com/limine-bootloader/limine.git "$LIMINE_DIR"
fi

# Build Limine if not already built
if [ ! -f "$LIMINE_SYS" ] || [ ! -f "$LIMINE_DEPLOY" ]; then
    echo "Building Limine (BIOS only)..."
    cd "$LIMINE_DIR"
    ./bootstrap
    ./configure --enable-bios
    make
    cd ..
else
    echo "Limine already built."
fi

# Final check
if [ ! -f "$LIMINE_SYS" ]; then
    echo "Error: limine-bios.sys not found after build."
    exit 1
fi

echo "Done. Limine BIOS + deployer ready."
