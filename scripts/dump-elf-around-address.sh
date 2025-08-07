#!/bin/sh

if [ $# -lt 2 ]; then
    echo "Usage: $0 <elf-file> <hex-address> [size] [around|from]"
    exit 1
fi

node dump-elf-around-address.js "$1" "$2" "${3:-64}" "$4"
