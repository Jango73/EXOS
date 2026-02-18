# Packaging System Implementation Roadmap

## Prerequisites (one-time)

- [x] **Read-only mount layer**: stable virtual filesystem mount/unmount flow for package volumes.
- [x] **Compression primitives**: zlib inflate path available in kernel for on-demand block decompression.
- [x] **Hashing primitives**: SHA-256 helper for package, file, and block validation.
- [x] **Signature primitives**: optional detached signature verification path.

## Step 1 --- Freeze `.epk` binary format

**Goal**: define a strict, versioned package format that can be validated at mount time.

- [x] Finalize fixed header fields (`Magic`, `Version`, offsets, sizes, flags).
- [x] Finalize table of contents entry layout (file, folder, folder alias, metadata).
- [x] Finalize block table layout for chunked compressed content.
- [x] Finalize manifest blob embedding and optional signature block.
- [x] Define compatibility policy for unknown flags and future versions.

**Success**: one specification file and one parser contract produce deterministic validation results.

## Step 2 --- Build package image tooling

**Goal**: generate valid `.epk` archives from input folders.

- [x] Implement host-side packaging tools in `tools/` (Linux first, cross-oriented workflow).
- [x] Add a pack command that walks input folders and writes deterministic table of contents ordering.
- [x] Add chunking and zlib compression with configurable chunk size.
- [x] Compute file hashes, block hashes, and package hash.
- [x] Embed `manifest.toml` as a dedicated section.
- [x] Add optional signature generation hook.

**Success**: repeated packaging of identical input produces byte-identical `.epk` output.

## Step 3 --- Kernel parser and validator

**Goal**: safely open and verify package metadata without exposing file content yet.

- [x] Implement header parsing and structural bounds checks.
- [x] Implement package hash validation.
- [x] Implement optional signature verification flow.
- [x] Parse table of contents and block table into kernel structures.
- [x] Reject malformed package files with explicit error logs.

**Success**: kernel can scan package folders and classify each package as valid or invalid.

## Step 4 --- `packagefs` readonly mount

**Goal**: mount one valid package as a virtual readonly filesystem tree.

- [ ] Create package filesystem driver module and mount entry points.
- [ ] Expose files, folders, and folder aliases from table of contents.
- [ ] Enforce readonly semantics for every operation.
- [ ] Add unmount cleanup path with object lifetime safety.

**Success**: browsing a mounted package shows expected tree and forbids writes.

## Step 5 --- On-demand block decompression

**Goal**: stream file bytes from compressed blocks without extracting package content on disk.

- [ ] Map file reads to block table ranges.
- [ ] Decompress required chunks only, with bounds and hash validation.
- [ ] Add reusable cache module for decompressed package chunks in `kernel/include/utils` + `kernel/source/utils`.
- [ ] Integrate cache eviction and invalidation on unmount.

**Success**: executables and libraries load directly from mounted packages with no temporary files.

## Step 6 --- System namespace integration

**Goal**: integrate package mounts into EXOS namespace model.

- [ ] Implement global scan sources:
  - [ ] `/library/package/`
  - [ ] `/apps/`
  - [ ] `/users/*/package/`
- [ ] Mount packages by role into expected locations.
- [ ] Add private process view for `/package`.
- [ ] Add `/user-data` alias to `/current-user/<package-name>/data`.

**Success**: packaged application process sees global mounts plus private `/package` and `/user-data`.

## Step 7 --- Manifest and dependency resolution

**Goal**: resolve package dependencies by provided API contracts.

- [ ] Parse `manifest.toml` model (`name`, `version`, `provides`, `requires`).
- [ ] Build provider index from mounted global packages.
- [ ] Validate all `requires` entries before activating package mount.
- [ ] Produce deterministic dependency failure diagnostics.

**Success**: missing or incompatible system dependencies block activation with clear reasons.

## Step 8 --- Atomic activation and rollback

**Goal**: make install, remove, update, and rollback state transitions atomic.

- [ ] Persist active package hashes in `/system-data/package/active.list`.
- [ ] Add transaction flow: stage, validate, commit.
- [ ] Add rollback flow to previous active set snapshot.
- [ ] Guarantee crash-safe state restoration after interrupted activation.

**Success**: power loss during update does not leave partial package activation state.

## Step 9 --- Userland package manager commands

**Goal**: expose basic package operations to users and automation.

- [ ] Keep `tools/` as the primary package creation path while native EXOS package management matures.
- [ ] Implement `package list` command.
- [ ] Implement `package add <file.epk>` command.
- [ ] Implement `package remove <package-name>` command.
- [ ] Implement `package verify` command.
- [ ] Report validation, dependency, and activation status in machine-readable and human-readable output.

**Success**: package lifecycle operations are scriptable and predictable.

## Step 10 --- Loader and runtime integration

**Goal**: ensure executable and shared library loading works with package search rules.

- [ ] Implement loader search order:
  - [ ] `/package/binary` first
  - [ ] `/library/package/<package-name>/binary` second
- [ ] Validate runtime symbol resolution with embedded and global shared components.
- [ ] Validate default packaged application work folder as `/user-data`.

**Success**: packaged applications start without extracted binaries and resolve libraries deterministically.

## Step 11 --- Security hardening

**Goal**: reduce attack surface in package parsing and mounting.

- [ ] Fuzz package parser with malformed headers and table entries.
- [ ] Enforce strict integer overflow checks for offsets and sizes.
- [ ] Enforce signature policy modes (disabled, optional, required).
- [ ] Add rate-limited warning/error logs for repeated invalid packages.

**Success**: malformed or hostile package files fail closed without kernel instability.

## Step 12 --- Observability and diagnostics

**Goal**: make package state and failures easy to inspect.

- [ ] Add structured kernel logs for scan, validate, mount, unmount, dependency decisions.
- [ ] Add shell command for mounted package graph inspection.
- [ ] Add system data view page for package status and hashes.

**Success**: support and debugging workflows identify package failures without source code tracing.

## Step 13 --- Validation matrix and release criteria

**Goal**: certify packaging behavior across supported architectures.

- [ ] Validate full flow on `x86-32` and `x86-64`.
- [ ] Validate boot with global system packages only.
- [ ] Validate packaged application launch with private dependencies.
- [ ] Validate update + rollback + reboot persistence.
- [ ] Validate negative paths (corrupt hash, bad signature, missing dependency, invalid folder alias).

**Success**: packaging passes automated smoke coverage and manual fault-injection scenarios.

## Step Decoupling (each step runs without the next)

- [ ] Steps 1-3: format and validation only, no mounted file access.
- [ ] Steps 4-5: readonly package filesystem and streaming reads.
- [ ] Steps 6-7: namespace model and dependency contracts.
- [ ] Steps 8-9: atomic lifecycle operations and user commands.
- [ ] Steps 10-13: runtime integration, hardening, and certification.
