#!/bin/bash
set -e

echo "Updating package lists..."
sudo apt update

echo "Installing EXOS development dependencies..."

sudo apt install -y \
    automake \
    binutils \
    clang \
    coreutils \
    dosfstools \
    findutils \
    grep \
    gzip \
    gawk \
    make \
    mtools \
    nasm \
    parted \
    qemu-system-x86 \
    sed \
    unzip \
    wget

echo "All required dependencies for EXOS installed."

echo "Downloading i686-elf toolchain (lordmilko)..."

TOOLCHAIN_URL=$(wget -qO- "https://api.github.com/repos/lordmilko/i686-elf-tools/releases/latest" | grep "browser_download_url" | grep linux | cut -d '"' -f 4 | head -1)
INSTALL_DIR="/opt/i686-elf-toolchain"
ARCHIVE_PATH="/tmp/$(basename "$TOOLCHAIN_URL")"

echo "Download URL: $TOOLCHAIN_URL"
echo "Install dir: $INSTALL_DIR"
echo "Archive: $ARCHIVE_PATH"

if [ -f "$ARCHIVE_PATH" ]; then
    echo "Archive already present: $ARCHIVE_PATH"
else
    echo "Downloading toolchain archive..."
    wget -O "$ARCHIVE_PATH" "$TOOLCHAIN_URL"
fi

sudo rm -rf "$INSTALL_DIR"
sudo mkdir -p "$INSTALL_DIR"

# Detect archive type and extract accordingly
if [[ "$ARCHIVE_PATH" =~ \.tar\.gz$ ]]; then
    echo "Extracting tar.gz archive..."
    sudo tar -xzf "$ARCHIVE_PATH" -C "$INSTALL_DIR" --strip-components=1
elif [[ "$ARCHIVE_PATH" =~ \.tar\.xz$ ]]; then
    echo "Extracting tar.xz archive..."
    sudo tar -xJf "$ARCHIVE_PATH" -C "$INSTALL_DIR" --strip-components=1
elif [[ "$ARCHIVE_PATH" =~ \.zip$ ]]; then
    echo "Extracting zip archive..."
    sudo unzip -o "$ARCHIVE_PATH" -d "$INSTALL_DIR"
else
    echo "Unknown archive extension for $ARCHIVE_PATH"
    exit 1
fi

# Fix PATH for lordmilko toolchain
if [ -d "$INSTALL_DIR/i686-elf-tools-linux/bin" ]; then
    TOOLCHAIN_PATH="$INSTALL_DIR/i686-elf-tools-linux/bin"
elif [ -d "$INSTALL_DIR/bin" ]; then
    TOOLCHAIN_PATH="$INSTALL_DIR/bin"
else
    echo "Cannot find toolchain bin directory."
    exit 1
fi

if ! grep -q "$TOOLCHAIN_PATH" ~/.bashrc; then
    echo "export PATH=\"\$PATH:$TOOLCHAIN_PATH\"" >> ~/.bashrc
    echo "Added $TOOLCHAIN_PATH to PATH in ~/.bashrc."
else
    echo "$TOOLCHAIN_PATH already in PATH."
fi

echo "i686-elf toolchain installed to $TOOLCHAIN_PATH."
echo "Done. You may need to restart your shell for PATH changes to take effect."
