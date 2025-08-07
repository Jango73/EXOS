#!/bin/bash
set -e

echo "Updating package lists..."
sudo apt update

echo "Installing EXOS development dependencies..."

sudo apt install -y \
    clang \
    gcc \
    make \
    nasm \
    binutils \
    xorriso \
    autoconf \
    automake \
    coreutils \
    findutils \
    gawk \
    sed \
    grep \
    gzip \
    mtools \
    dosfstools \
    qemu-system-x86

echo "All required dependencies for EXOS installed."
