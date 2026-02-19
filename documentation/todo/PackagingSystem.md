# System Packaging Concept for EXOS

## 1. Overview

The EXOS packaging system is based on **compressed, read-only package archives** (`.epk`) mounted as virtual filesystems.
Each application package is **self-contained**: executable, private libraries, resources, and metadata are all inside the package.

This model ensures:
- immutable application payloads,
- deterministic launch behavior,
- no partial installation states,
- no dependency solver and no transitive resolution,
- simple launch flow: verify, mount, run.

Conceptually, package management is a **mount-and-run** workflow, not an installer workflow.

**Policy guardrail (non-negotiable):**
- EXOS does **not** implement a dependency solver.
- EXOS does **not** perform transitive dependency resolution.
- EXOS does **not** auto-install implicit dependencies.
- Application packages must embed all non-system libraries.
- EXOS does **not** define or manage "system packages".

---

## 2. Design Principles

1. **Mount, not extract**
   Packages stay as `.epk` archives and are mounted by `packagefs`.

2. **Application self-containment**
   Application runtime content lives in the package itself (`/package/binary`, `/package/data`).

3. **On-demand activation**
   A package is verified and mounted when the user launches it.
   No global package activation set is required.

4. **Simple namespace view**
   A packaged process receives:
   - `/package` (private mount of its `.epk`),
   - `/user-data` (alias to `/current-user/<package-name>/data`).

5. **Compression transparency**
   Files are zlib-compressed in chunks and decompressed on demand.

6. **Minimal tooling**
   Packaging requires:
   - `packagefs` support,
   - host-side package creation tools (`tools/`),
   - optional remote fetch tooling (distribution only).

---

## 3. Package Structure

Each `.epk` is one archive with metadata tables and chunked compressed file content.

### 3.1 File Layout (binary)

**Header (fixed 128 bytes, little-endian)**
- Magic: `EPK1`
- Version: `U32`
- Flags: `U32` (compression, signed, encrypted)
- HeaderSize: `U32`
- TocOffset: `U64`, TocSize: `U64`
- BlockTableOffset: `U64`, BlockTableSize: `U64`
- ManifestOffset: `U64`, ManifestSize: `U64`
- SignatureOffset: `U64`, SignatureSize: `U64`
- PackageHash: SHA-256 over all bytes except signature block
- Reserved: zero-filled to 128 bytes

**TOC (Table of Contents)**
- PathLength: `U32`, PathBytes (UTF-8)
- NodeType: `U32` (file, folder, folder alias)
- Permissions: `U32`
- ModifiedTime: `DATETIME`
- FileSize: `U64`
- DataOffset: `U64`
- BlockIndexStart: `U32`, BlockCount: `U32`
- FileHash: SHA-256 of uncompressed data
- FolderAliasTargetLength + FolderAliasTarget

**DATETIME (64-bit packed fields, little-endian)**
- Year: 26 bits
- Month: 4 bits
- Day: 6 bits
- Hour: 6 bits
- Minute: 6 bits
- Second: 6 bits
- Milli: 10 bits

**Data Region**
Chunked file payloads (default 64 KiB uncompressed per chunk), zlib-compressed.

**Block Table**
- CompressedOffset: `U64`
- CompressedSize: `U32`
- UncompressedSize: `U32`
- ChunkHash: SHA-256

**Manifest**
UTF-8 TOML metadata blob (same content as `/package/manifest.toml`).

**Signature (optional)**
Detached signature over `PackageHash`.

---

## 4. Package Launch Mount Process

1. **Launch request**
   User or shell asks to execute a packaged application from a `.epk` file.

2. **Validation**
   Kernel validates header, bounds, checksums, and optional signature.

3. **Compatibility checks**
   Kernel validates manifest constraints (`arch`, `kernel.api`, policy fields).

4. **Private mount**
   Package is mounted in a private process view at `/package`.

5. **User data alias**
   `/user-data` is mapped to `/current-user/<package-name>/data`.

6. **Execution**
   Loader streams executable and private libraries directly from package blocks.

---

## 5. Execution Model

- **Binary and library loading**
  Loader reads executable and libraries from `/package/binary` first.

- **System runtime boundary**
  The OS may expose a small fixed set of system runtime libraries (kernel-adjacent ABI/runtime), but these are not packages.

- **No global dependency resolution**
  Package content remains self-contained. Missing embedded libraries are packaging errors.

- **Caching**
  Decompressed chunks/pages may be cached by kernel memory subsystems.

### Process view (packaged app)

```
/
|-- system/
|-- users/
|-- current-user/         (folder alias -> /users/<user-name>)
|-- package/              (private mount)
|   |-- binary/
|   |-- data/
|   `-- manifest.toml
`-- user-data/            (alias -> /current-user/<package-name>/data)
```

Default packaged app work folder: `/user-data`.

---

## 6. Manifest and Compatibility Model

Example `manifest.toml`:

```toml
name = "app.video-editor"
version = "2.4.1"
arch = "x86-64"
kernel_api = ">=1.0"
entry = "/package/binary/video-editor"
```

Manifest goals:
- package identity,
- deterministic launch policy,
- direct kernel/system compatibility checks.

Validation rules:
- `arch` must match target runtime architecture,
- `kernel_api` must satisfy kernel compatibility policy,
- manifest must be structurally valid and bounded,
- no `requires/provides` dependency graph behavior.

---

## 6.1 Embedded Runtime Policy

- Application libraries are embedded in the package.
- EXOS does not maintain a global application library pool.
- The only non-embedded runtime components are the kernel and the small fixed system runtime boundary.

This prevents dependency hell by design.

---

## 6.2 Remote Repository (Planned, Not Implemented)

EXOS may support explicit package download from remote repositories.

Planned scope:
- signed repository index,
- explicit fetch command,
- package hash/signature verification,
- local stage then launch.

Not planned:
- dependency solver,
- transitive auto-fetch,
- background auto-resolution.

Remote repositories are a distribution mechanism, not a dependency-resolution mechanism.

---

## 7. Package Lifecycle Semantics

- **Install/store**: copy `.epk` into a user-selected folder.
- **Launch**: verify + mount privately + run.
- **Stop**: unmount private package view for the process.
- **Update**: replace `.epk` file with a new one.

No global package activation state is required for application launch semantics.

---

## 8. Advantages Summary

| Feature | Description |
|----------|-------------|
| Simplicity | Verify, mount, run |
| Isolation | App runtime is package-contained |
| Reproducibility | Same `.epk` yields same runtime payload |
| Safety | Read-only mount semantics |
| Stability | No dependency hell |
| Performance | Chunked compression + on-demand IO |

---

## 9. Future Extensions

- Remote repository integration (explicit fetch only)
- Delta package transport
- Per-app sandbox policy descriptors
- Optional package content encryption

---

## 10. Summary

The EXOS packaging model is a mount-based application delivery system.
Applications carry their own runtime payload, are validated at launch, and run from a private mounted package view.

This removes dependency resolver complexity and keeps runtime behavior deterministic.

---

## 11. Concrete Example

```
/users/alice/package/
`-- video-editor.epk
```

At launch, process view includes:

```
/package
/user-data
```

No global application package mount graph is required.
