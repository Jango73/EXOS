# Build Output Layout Refactor Plan

## Goal
Isolate build artifacts by architecture and effective build options, while avoiding redundant core rebuilds when only the filesystem image type changes.

Target naming examples:
- core: `build/core/x86-32-mbr-debug`
- core: `build/core/x86-64-uefi-debug-split`
- image: `build/image/x86-64-mbr-release-ext2`
- image: `build/image/x86-64-mbr-release-fat32`

## Naming Rules
- [ ] Define one canonical core identifier: `BUILD_CORE_NAME`.
- [ ] Compose `BUILD_CORE_NAME` from:
  - architecture: `x86-32` or `x86-64`
  - boot mode: `mbr` or `uefi`
  - configuration: `debug` or `release`
  - optional suffixes: `split` (only when enabled)
- [ ] Ensure deterministic order of segments:
  - `<arch>-<boot>-<config>[-split]`
- [ ] Define one canonical image identifier: `BUILD_IMAGE_NAME`.
- [ ] Compose `BUILD_IMAGE_NAME` as:
  - `<BUILD_CORE_NAME>-<filesystem>`
  - where filesystem is `ext2` or `fat32`

## Phase 1 - Build Pipeline Core
- [ ] Update `scripts/build.sh` to compute both `BUILD_CORE_NAME` and `BUILD_IMAGE_NAME`.
- [ ] Move build lock to `build/core/<BUILD_CORE_NAME>/.build-lock`.
- [ ] Export both identifiers to `make` sub-invocations.
- [ ] Keep backward compatibility fallback during migration.

## Phase 2 - Makefiles Path Migration
- [ ] Root `Makefile`: propagate `BUILD_CORE_NAME` and `BUILD_IMAGE_NAME` in `SUBMAKE`.
- [ ] `kernel/Makefile`: place outputs in `../build/core/$(BUILD_CORE_NAME)` (with fallback).
- [ ] `runtime/Makefile`: same migration.
- [ ] `runtime/make/exos.mk`: point runtime lib + apps to core output path.
- [ ] `system/Makefile` and sub-app outputs: validate generated artifacts resolve via core path.
- [ ] `tools` Makefile(s): migrate output paths to core path.
- [ ] `boot-hd/Makefile`: read binaries from core path, write images/staging to image path.
- [ ] `boot-uefi/Makefile`: read binaries from core path, write image/staging/OVMF vars to image path.
- [ ] `boot-freedos/Makefile`: read binaries from core path, write image output to image path.

## Phase 3 - Run/Test Scripts
- [ ] Update `scripts/run.sh` to resolve artifacts from `build/image/<BUILD_IMAGE_NAME>` and symbols from `build/core/<BUILD_CORE_NAME>`.
- [ ] Add explicit option to `run.sh` if needed (`--build-core-name` / `--build-image-name`) for deterministic selection.
- [ ] Update `scripts/4-1-smoke-test.sh` to pass matching build/run parameters and paths.
- [ ] Verify log naming remains unchanged or intentionally adapted (no ambiguity between targets).

## Phase 4 - Utility Scripts and Secondary Tooling
- [ ] Update debug helpers using hardcoded build paths:
  - `scripts/utils/addr2src-kernel-x86-64.sh`
  - `scripts/utils/trace-exos-eip.sh`
  - `scripts/utils/trace-exos-rip.sh`
  - `scripts/utils/dump-exos-bin-i386.sh`
  - `scripts/utils/dump-exos-bin-x86-64.sh`
  - any other `scripts/x86-*` helper reading `build/<arch>/...`
- [ ] Update Windows runner `scripts/run.bat` to align core/image path logic.
- [ ] Update remote scripts only if they consume artifact paths directly.

## Phase 5 - Documentation
- [ ] Update `AGENTS.md` build command/output notes with the new artifact layout.
- [ ] Update `documentation/Kernel.md` smoke-test/build path references.
- [ ] Add one short mapping table: old path -> new path.

## Validation Matrix
- [ ] `x86-32 + mbr + debug + ext2`
- [ ] `x86-64 + mbr + debug + ext2`
- [ ] `x86-64 + uefi + debug + ext2`
- [ ] `x86-64 + uefi + release + ext2`
- [ ] `x86-32 + mbr + release + fat32`
- [ ] `x86-64 + mbr + release + fat32`
- [ ] `x86-64 + uefi + debug + ext2 + split`
- [ ] `x86-64 + uefi + release + ext2 + split`

## Compatibility and Rollout
- [ ] Keep fallback compatibility during transition.
- [ ] Migrate all primary scripts first (`build.sh`, `run.sh`, smoke-test).
- [ ] Remove fallback only after all scripts and docs are fully migrated.

## Risks and Controls
- [ ] Risk: run/build mismatch selects wrong core/image combination.
  - Control: derive both names from the same option set and expose optional explicit overrides.
- [ ] Risk: stale scripts still point to `build/x86-32` or `build/x86-64`.
  - Control: global grep audit before finalizing.
- [ ] Risk: concurrent builds collide.
  - Control: per-core lock directory (`build/core/<BUILD_CORE_NAME>/.build-lock`).
