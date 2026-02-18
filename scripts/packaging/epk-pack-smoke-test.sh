#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_CORE_NAME="${BUILD_CORE_NAME:-x86-64-mbr-debug}"
PACK_TOOL="$ROOT_DIR/build/core/$BUILD_CORE_NAME/tools/epk-pack"
VERIFY_TOOL="$ROOT_DIR/scripts/packaging/verify-epk.js"
WORK_DIR="${WORK_DIR:-/tmp/exos-epk-pack-smoke}"

function Usage() {
    echo "Usage: $0 [--build-core-name <name>] [--work-dir <path>] [--no-build]"
}

DO_BUILD=1
while [ $# -gt 0 ]; do
    case "$1" in
        --build-core-name)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --build-core-name"
                exit 1
            fi
            BUILD_CORE_NAME="$1"
            PACK_TOOL="$ROOT_DIR/build/core/$BUILD_CORE_NAME/tools/epk-pack"
            ;;
        --work-dir)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing value for --work-dir"
                exit 1
            fi
            WORK_DIR="$1"
            ;;
        --no-build)
            DO_BUILD=0
            ;;
        --help|-h)
            Usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            Usage
            exit 1
            ;;
    esac
    shift
done

if [ "$DO_BUILD" -eq 1 ]; then
    echo "[epk-pack-smoke] Building tools for $BUILD_CORE_NAME"
    make -C "$ROOT_DIR/tools" BUILD_CORE_NAME="$BUILD_CORE_NAME" >/tmp/epk-pack-build.log 2>&1 || {
        cat /tmp/epk-pack-build.log
        exit 1
    }
fi

if [ ! -x "$PACK_TOOL" ]; then
    echo "Missing tool: $PACK_TOOL"
    exit 1
fi

if ! command -v node >/dev/null 2>&1; then
    echo "Missing node runtime"
    exit 1
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/input/bin" "$WORK_DIR/input/lib" "$WORK_DIR/input/assets"

cat > "$WORK_DIR/input/manifest.toml" << 'MANIFEST'
name = "smoke.demo"
version = "1.0.0"
MANIFEST

# Mix compressible and less-compressible payloads
printf 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n' > "$WORK_DIR/input/bin/repeat.txt"
head -c 8192 /dev/urandom > "$WORK_DIR/input/assets/random.bin"
printf 'hello package world\n' > "$WORK_DIR/input/lib/core.txt"

echo "[epk-pack-smoke] Test 1: deterministic output (default zlib)"
"$PACK_TOOL" pack --input "$WORK_DIR/input" --output "$WORK_DIR/a.epk"
"$PACK_TOOL" pack --input "$WORK_DIR/input" --output "$WORK_DIR/b.epk"

cmp -s "$WORK_DIR/a.epk" "$WORK_DIR/b.epk" || {
    echo "Determinism check failed: a.epk != b.epk"
    exit 1
}

node "$VERIFY_TOOL" --file "$WORK_DIR/a.epk" --expect-signature-flag 0 --expect-signature-size 0 --expect-min-toc-entries 5

echo "[epk-pack-smoke] Test 2: compression=none"
"$PACK_TOOL" pack --input "$WORK_DIR/input" --output "$WORK_DIR/none.epk" --compression none
node "$VERIFY_TOOL" --file "$WORK_DIR/none.epk" --expect-signature-flag 0 --expect-signature-size 0 --expect-compression-method none --expect-min-toc-entries 5

echo "[epk-pack-smoke] Test 3: compression=auto (mixed expected)"
"$PACK_TOOL" pack --input "$WORK_DIR/input" --output "$WORK_DIR/auto.epk" --compression auto
node "$VERIFY_TOOL" --file "$WORK_DIR/auto.epk" --expect-signature-flag 0 --expect-signature-size 0 --expect-compression-method mixed --expect-min-toc-entries 5

echo "[epk-pack-smoke] Test 4: signature hook"
"$PACK_TOOL" pack --input "$WORK_DIR/input" --output "$WORK_DIR/signed.epk" --signature-command "printf 5349474e" --signature-output hex
node "$VERIFY_TOOL" --file "$WORK_DIR/signed.epk" --expect-signature-flag 1 --expect-signature-size 4 --expect-min-toc-entries 5

echo "[epk-pack-smoke] All tests passed"
