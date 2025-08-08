#!/bin/bash
set -e

echo "Updating package lists..."
sudo apt update

echo "Installing EXOS development dependencies..."

sudo apt install -y \
    clang \
    make \
    nasm \
    binutils \
    automake \
    coreutils \
    findutils \
    gawk \
    sed \
    grep \
    gzip \
    mtools \
    dosfstools \
    qemu-system-x86 \
    wget

echo "All required dependencies for EXOS installed."

echo "Downloading i686-elf toolchain (lordmilko)..."

TOOLCHAIN_URL=$(wget -qO- "https://api.github.com/repos/lordmilko/i686-elf-tools/releases/latest" | grep "browser_download_url" | grep linux | cut -d '"' -f 4 | head -1)
INSTALL_DIR="/opt/i686-elf-toolchain"

sudo rm -rf "$INSTALL_DIR"
sudo mkdir -p "$INSTALL_DIR"
sudo wget -O /tmp/i686-elf-toolchain.tar.gz "$TOOLCHAIN_URL"
sudo tar -xzf /tmp/i686-elf-toolchain.tar.gz -C "$INSTALL_DIR" --strip-components=1
rm /tmp/i686-elf-toolchain.tar.gz

echo "i686-elf toolchain installed to $INSTALL_DIR."

# Add to PATH if not already present
if ! grep -q "$INSTALL_DIR/bin" ~/.bashrc; then
    echo "export PATH=\"\$PATH:$INSTALL_DIR/bin\"" >> ~/.bashrc
    echo "Added $INSTALL_DIR/bin to PATH in ~/.bashrc."
else
    echo "$INSTALL_DIR/bin already in PATH."
fi

echo "Done. You may need to restart your shell for PATH changes to take effect."
