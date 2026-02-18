#!/bin/bash
set -euo pipefail

QEMU_VERSION="9.0.2"
QEMU_ARCHIVE="qemu-${QEMU_VERSION}.tar.xz"
QEMU_URL="https://download.qemu.org/${QEMU_ARCHIVE}"

check_qemu_installed() {
    if command -v qemu-system-i386 >/dev/null 2>&1; then
        local installed_version
        installed_version=$(qemu-system-i386 --version | head -1 | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' | head -1)
        echo "QEMU version $installed_version is already installed"
        if [ "$installed_version" = "$QEMU_VERSION" ]; then
            echo "Required version ($QEMU_VERSION) already installed. Exiting."
            exit 0
        else
            echo "Different version detected. Proceeding with installation..."
        fi
    fi
}

install_dependencies() {
    echo "Installing dependencies..."
    sudo apt update
    sudo apt install -y build-essential git ninja-build pkg-config libglib2.0-dev libpixman-1-dev libsdl2-dev
}

download_and_build_qemu() {
    local temp_dir
    temp_dir=$(mktemp -d -t qemu-build-XXXXXX)

    echo "Using temporary directory: $temp_dir"

    trap "echo 'Cleaning up temporary directory...'; rm -rf '$temp_dir'" EXIT

    cd "$temp_dir"

    echo "Downloading QEMU $QEMU_VERSION..."
    wget "$QEMU_URL"

    echo "Extracting archive..."
    tar -xf "$QEMU_ARCHIVE"

    cd "qemu-$QEMU_VERSION"

    echo "Configuring..."
    ./configure --target-list=x86-32-softmmu,x86_64-softmmu --enable-kvm --enable-sdl --enable-slirp --enable-vnc --disable-spice --enable-gtk

    echo "Building (using $(nproc) processors)..."
    make -j$(nproc)

    echo "Installing..."
    sudo make install

    echo "QEMU $QEMU_VERSION installed successfully!"
}

main() {
    echo "=== QEMU $QEMU_VERSION Installation ==="

    check_qemu_installed
    install_dependencies
    download_and_build_qemu
}

main "$@"
