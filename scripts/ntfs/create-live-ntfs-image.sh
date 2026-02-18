#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

IMAGE_PATH="$ROOT_DIR/build/test-images/ntfs-live.img"
IMAGE_SIZE_MIB=80
RECREATE=0
MUTATION_ROUNDS=1
LABEL="PREDATOR"

LOOP_DEVICE=""
PARTITION_DEVICE=""
MOUNT_DIR=""

usage() {
    cat <<EOF
Usage: $0 [options]

Options:
  --output <path>       Output image path (default: build/test-images/ntfs-live.img)
  --size-mib <n>        Image size in MiB when creating (default: 4096)
  --label <name>        NTFS label (default: PREDATOR)
  --rounds <n>          Mutation rounds to apply (default: 1)
  --recreate            Recreate the image from scratch
  --help                Show this help

Notes:
  - This script needs root privileges (losetup/mount/umount).
  - If the image already exists and --recreate is not passed, the script mutates it in-place.
EOF
}

require_command() {
    local command_name="$1"
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Missing command: $command_name"
        exit 1
    }
}

cleanup() {
    set +e
    if [ -n "$MOUNT_DIR" ] && mountpoint -q "$MOUNT_DIR"; then
        umount "$MOUNT_DIR"
    fi
    if [ -n "$LOOP_DEVICE" ]; then
        losetup -d "$LOOP_DEVICE"
    fi
    if [ -n "$MOUNT_DIR" ] && [ -d "$MOUNT_DIR" ]; then
        rmdir "$MOUNT_DIR" >/dev/null 2>&1 || true
    fi
    if [ -f "$IMAGE_PATH" ]; then
        set_output_ownership "$IMAGE_PATH"
    fi
    if [ -d "$(dirname "$IMAGE_PATH")" ]; then
        set_output_ownership "$(dirname "$IMAGE_PATH")"
    fi
}

set_output_ownership() {
    local target="$1"
    local uid="${SUDO_UID:-}"
    local gid="${SUDO_GID:-}"

    if [ -n "$uid" ] && [ -n "$gid" ]; then
        chown "$uid":"$gid" "$target" || true
    elif [ -n "${SUDO_USER:-}" ]; then
        chown "$SUDO_USER":"$SUDO_USER" "$target" || true
    fi
}

create_partitioned_image() {
    local image="$1"
    local size_mib="$2"

    mkdir -p "$(dirname "$image")"
    truncate -s "$((size_mib * 1024 * 1024))" "$image"
    parted -s "$image" mklabel msdos
    parted -s "$image" mkpart primary ntfs 1MiB 100%
    parted -s "$image" set 1 boot on
}

attach_image() {
    local image="$1"

    LOOP_DEVICE="$(losetup --find --show --partscan "$image")"
    PARTITION_DEVICE="${LOOP_DEVICE}p1"
    if [ ! -b "$PARTITION_DEVICE" ]; then
        sleep 0.2
    fi
    if [ ! -b "$PARTITION_DEVICE" ]; then
        echo "Unable to find partition device for $image ($PARTITION_DEVICE)"
        exit 1
    fi
}

format_ntfs_partition() {
    local device="$1"
    local label="$2"
    mkntfs -F -L "$label" "$device" >/dev/null
}

mount_ntfs_partition() {
    local device="$1"
    MOUNT_DIR="$(mktemp -d /tmp/exos-ntfs-live.XXXXXX)"

    if mount -t ntfs3 "$device" "$MOUNT_DIR" 2>/dev/null; then
        return 0
    fi
    if mount -t ntfs-3g "$device" "$MOUNT_DIR" 2>/dev/null; then
        return 0
    fi

    echo "Unable to mount NTFS partition ($device). Need ntfs3 kernel driver or ntfs-3g."
    exit 1
}

ensure_windows_layout() {
    local root="$1"

    mkdir -p "$root/Renders"
    mkdir -p "$root/OneDriveTemp"
    mkdir -p "$root/Recovery"
    mkdir -p "$root/System Volume Information"
    mkdir -p "$root/Documents and Settings"
    mkdir -p "$root/\$Recycle.Bin"
    mkdir -p "$root/Windows/WinSxS"
    mkdir -p "$root/Windows/System32/drivers"
    mkdir -p "$root/Users/Default/AppData/Local/Temp"
    mkdir -p "$root/Users/Public/Documents"
    mkdir -p "$root/ProgramData/Microsoft/Search/Data"
    mkdir -p "$root/Program Files/Common Files"
    mkdir -p "$root/Program Files (x86)/Common Files"
    mkdir -p "$root/PerfLogs"
    mkdir -p "$root/Temp"
    mkdir -p "$root/Swap"

    if [ ! -f "$root/bootmgr" ]; then
        dd if=/dev/zero of="$root/bootmgr" bs=1M count=1 status=none
    fi
    if [ ! -f "$root/pagefile.sys" ]; then
        dd if=/dev/zero of="$root/pagefile.sys" bs=1M count=4 status=none
    fi
    if [ ! -f "$root/swapfile.sys" ]; then
        dd if=/dev/zero of="$root/swapfile.sys" bs=1M count=1 status=none
    fi

    echo "EXOS NTFS live test image" > "$root/README-EXOS-NTFS.txt"
}

write_random_file() {
    local target="$1"
    local kib="$2"
    dd if=/dev/urandom of="$target" bs=1024 count="$kib" status=none
}

mutate_ntfs_content() {
    local root="$1"
    local round="$2"
    local i=0

    mkdir -p "$root/Renders"
    mkdir -p "$root/Temp"
    mkdir -p "$root/Users/Public/Documents"

    # Fragmentation phase A: create many small fillers.
    for i in $(seq 1 80); do
        local kib=$((32 + (i % 24) * 8))
        write_random_file "$root/Temp/fill-r${round}-${i}.bin" "$kib"
    done

    # Fragmentation phase B: punch holes in free space by deleting half the fillers.
    for i in $(seq 1 80); do
        if [ $((i % 2)) -eq 1 ]; then
            rm -f "$root/Temp/fill-r${round}-${i}.bin"
        fi
    done

    # Fragmentation phase C: refill with variable medium files.
    for i in $(seq 1 220); do
        local kib=$((16 + (i % 20) * 4))
        write_random_file "$root/Renders/render-r${round}-${i}.dat" "$kib"
    done

    for i in $(seq 1 260); do
        local kib=$((8 + (i % 16) * 2))
        write_random_file "$root/Users/Public/Documents/doc-r${round}-${i}.bin" "$kib"
    done

    # Hot churn on a subset of files.
    for i in $(seq 1 120); do
        local target="$root/Renders/render-r${round}-$((1 + (i % 220))).dat"
        if [ -f "$target" ]; then
            dd if=/dev/urandom of="$target" bs=1024 count=$((2 + (i % 6))) conv=notrunc status=none
            cat "$target" >> "$root/Renders/churn.log" 2>/dev/null || true
        fi
    done

    # Simulate windows-like update churn and file relocations.
    for i in $(seq 1 60); do
        local src="$root/Users/Public/Documents/doc-r${round}-$((1 + (i % 260))).bin"
        local dst="$root/Recovery/recovered-r${round}-${i}.bin"
        if [ -f "$src" ]; then
            mv "$src" "$dst"
        fi
    done

    for i in $(seq 1 30); do
        rm -f "$root/Recovery/recovered-r${round}-${i}.bin"
    done

    echo "round=$round date=$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$root/System Volume Information/exos-ntfs-history.log"
    sync
}

while [ $# -gt 0 ]; do
    case "$1" in
        --output)
            shift
            IMAGE_PATH="$1"
            ;;
        --size-mib)
            shift
            IMAGE_SIZE_MIB="$1"
            ;;
        --label)
            shift
            LABEL="$1"
            ;;
        --rounds)
            shift
            MUTATION_ROUNDS="$1"
            ;;
        --recreate)
            RECREATE=1
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
    shift
done

if [ "$EUID" -ne 0 ]; then
    echo "Run as root (sudo)."
    exit 1
fi

require_command parted
require_command losetup
require_command mkntfs
require_command mount
require_command umount
require_command truncate
require_command dd

trap cleanup EXIT

if [ "$RECREATE" -eq 1 ] || [ ! -f "$IMAGE_PATH" ]; then
    echo "Creating NTFS image: $IMAGE_PATH"
    create_partitioned_image "$IMAGE_PATH" "$IMAGE_SIZE_MIB"
    attach_image "$IMAGE_PATH"
    format_ntfs_partition "$PARTITION_DEVICE" "$LABEL"
    mount_ntfs_partition "$PARTITION_DEVICE"
    ensure_windows_layout "$MOUNT_DIR"
else
    echo "Reusing existing NTFS image: $IMAGE_PATH"
    attach_image "$IMAGE_PATH"
    mount_ntfs_partition "$PARTITION_DEVICE"
fi

for round in $(seq 1 "$MUTATION_ROUNDS"); do
    mutate_ntfs_content "$MOUNT_DIR" "$round"
done

if [ -n "${SUDO_USER:-}" ]; then
    IMAGE_DIR="$(dirname "$IMAGE_PATH")"
    set_output_ownership "$IMAGE_PATH"
    set_output_ownership "$IMAGE_DIR"
fi
chmod 666 "$IMAGE_PATH" || true

echo "NTFS live image ready: $IMAGE_PATH"
