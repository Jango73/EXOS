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

EXT="${ARCHIVE_PATH##*.}"

if [[ "$EXT" == "gz" ]]; then
    TAR_OPT="xzf"
elif [[ "$EXT" == "xz" ]]; then
    TAR_OPT="xJf"
else
    echo "Unknown archive extension: $EXT"
    exit 1
fi

echo "Extracting archive..."
sudo tar -$TAR_OPT "$ARCHIVE_PATH" -C "$INSTALL_DIR" --strip-components=1

echo "i686-elf toolchain installed to $INSTALL_DIR."

if ! grep -q "$INSTALL_DIR/bin" ~/.bashrc; then
    echo "export PATH=\"\$PATH:$INSTALL_DIR/bin\"" >> ~/.bashrc
    echo "Added $INSTALL_DIR/bin to PATH in ~/.bashrc."
else
    echo "$INSTALL_DIR/bin already in PATH."
fi

echo "Done. You may need to restart your shell for PATH changes to take effect."
