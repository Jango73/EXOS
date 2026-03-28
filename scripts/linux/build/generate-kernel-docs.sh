#!/bin/bash

# Script to generate kernel documentation using doxygen

set -e

# Check if doxygen is installed
if ! command -v doxygen &> /dev/null; then
    echo "Error: doxygen is not installed. Please install it first:"
    echo "sudo apt-get install doxygen"
    exit 1
fi

echo "Generating kernel documentation with doxygen..."
cd "$(dirname "$0")/.."

rm -rf documentation/kernel

mkdir documentation/kernel

# Run doxygen with project Doxyfile
doxygen Doxyfile

echo ""
echo "Documentation is now in documentation/kernel/html/index.html"
