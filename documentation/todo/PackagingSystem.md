# System Packaging Concept for EXOS

## 1. Overview

The EXOS packaging system is based on **compressed, read-only package archives** that are *mounted* as virtual filesystems rather than extracted on disk.  
Each package acts as a **self-contained volume**, containing binaries, libraries, and metadata.  
When mounted, these packages integrate seamlessly into the global system namespace.

This model ensures:
- immutability of installed packages,
- atomic updates and rollbacks,
- efficient use of disk space through compression,
- reproducibility of system states,
- zero-risk upgrades (no partial installations).

In short: EXOS treats packages as immutable, compressed volumes mounted by the kernel, not unpacked.  
System components are shared globally, while application-specific libraries remain inside each package for isolation.  
Updates and rollbacks are atomic because the system state is just a list of mounted package files.

Conceptually, the package manager is not an "installer" -- it is a **volume orchestrator**.

---

## 2. Design Principles

1. **Mount, not copy**  
   Packages remain compressed archives (`.epk`) and are mounted by the kernel's *packagefs* rather than unpacked.

2. **Read-only system base**  
   The core system runs entirely from mounted packages.  
   Packaged app data is read-only in `/package/data` and dynamic app data uses `/user-data` which is an alias to `/current-user/<package-name>/data`.

3. **Atomic state changes**  
   Installing or removing a package = adding or removing a single file.  
   The kernel rebuilds the active package graph in real-time.

4. **Namespace layout**  
   Packages are mounted by role:
   ```
   /library/package/ -> system shared packages (runtime, drivers, stack), one mount per package:
                        /library/package/<package-name>/
   /apps/            -> application packages (global)
   /users/           -> per-user package files under /users/<user-name>/package
   ```
   Applications see all mounts from configuration, plus a private `/package` view.



5. **Compression transparency**  
   Files inside packages are zlib-compressed and transparently decompressed on demand.  
   Executables and libraries are never extracted to disk; they are streamed directly into memory at load time.

6. **Minimal external tooling**  
   Package management requires only:
   - filesystem support for `packagefs`,
   - a userland tool for adding/removing `.epk` files,
   - a daemon (optional) for dependency resolution.

---

## 3. Package Structure

Each package (`.epk`) is a **single compressed archive** with an internal folder tree and metadata table.

### 3.1 File Layout (binary)

The `.epk` format is designed for mount-time validation, fast metadata lookup, and on-demand decompression.

**Header (fixed 128 bytes, little-endian)**
- Magic: `EPK1`
- Version: `U32`
- Flags: `U32` (bitfield: compression, signed, encrypted)
- HeaderSize: `U32`
- TocOffset: `U64`, TocSize: `U64`
- BlockTableOffset: `U64`, BlockTableSize: `U64`
- ManifestOffset: `U64`, ManifestSize: `U64`
- SignatureOffset: `U64`, SignatureSize: `U64`
- PackageHash: SHA-256 over all bytes except the signature block
- Reserved: remaining bytes up to 128 bytes (must be zero)

**TOC (Table of Contents)**
Each entry describes a path and how to access its data.
- PathLength: `U32`, PathBytes (UTF-8)
- NodeType: `U32` (file, folder, folder alias)
- Permissions: `U32`
- ModifiedTime: `DATETIME`
- FileSize: `U64`
- DataOffset: `U64` (into data region, for small inline files)
- BlockIndexStart: `U32`, BlockCount: `U32` (for chunked files)
- FileHash: SHA-256 of uncompressed file data
- FolderAliasTargetLength + FolderAliasTarget (for folder aliases)

**DATETIME (stored as 64-bit packed fields, little-endian)**
- Year: 26 bits
- Month: 4 bits
- Day: 6 bits
- Hour: 6 bits
- Minute: 6 bits
- Second: 6 bits
- Milli: 10 bits

**Data Region**
File content is stored as independently compressed chunks (default 64 KiB uncompressed).
Each chunk is compressed with zlib to allow random access without full extraction.

**Block Table**
Indexed by `BlockIndexStart` and `BlockCount`.
Each entry provides:
- CompressedOffset: `U64`
- CompressedSize: `U32`
- UncompressedSize: `U32`
- ChunkHash: SHA-256

**Manifest**
A UTF-8 TOML document with the package metadata (same content as `/package/manifest.toml`).
The manifest is stored as a dedicated blob for fast dependency checks at mount time.

**Signature (optional)**
Detached signature over `PackageHash`. The format is flexible (e.g., ed25519).

---

## 4. Package Mounting Process

1. **Detection**  
   At boot or runtime, the system scans known package folders:
   ```
   /library/package/
   /apps/
   /users/*/package/
   ```

2. **Validation**  
   Each `.epk` file is verified (header, checksum, optional signature).

3. **Mounting**  
   The kernel mounts the package into a virtual read-only view.  
   Files inside the package become visible in the global namespace:

4. **On-demand decompression**  
   When a file is accessed, the filesystem reads its compressed blocks from the package and decompresses them in memory.  
   Pages are cached transparently by the kernel's VM.

5. **Unloading**  
   Removing a package simply unmounts it; no residual files remain.

---

## 5. Execution Model

- **Binaries and Libraries**  
  When the ELF loader requests an executable or `.so`, the `packagefs` supplies the decompressed bytes directly to memory.
  No temporary files are created.

- **Caching**  
  Frequently used pages are cached in RAM for instant reuse.  

- **Isolation**  
  Each process inherits a view of the mounted package graph according to its namespace.  
  Packaged apps see all mounts from configuration plus their private `/package` view.

### Application View Graph (Packaged App)

```
/
|-- system/               (global root, from configuration)
|-- library/package/      (global shared packages)
|-- apps/                 (application packages)
|-- users/                (all user folders, each may contain package files)
|-- current-user/         (current user folder alias -> /users/<user-name>)
|-- package/              (private mount of the application package)
    |-- binary/
    |-- data/
    |-- manifest.toml
|-- user-data/            (alias to folder /current-user/<package-name>/data)
```

Library lookup order: `/package/binary` then `/library/package/<package-name>/binary`.
Default packaged app work folder: `/user-data`.
Application data alias: `/user-data` -> `/current-user/<package-name>/data`.

---

## 6. Dependency and Version Management

Each package includes a `manifest.toml` file describing:
```toml
name = "core.graphics"
version = "2.4.1"
provides = ["api.graphics", "api.window"]

[[requires]]
api = "core.kernel"
version = ">=1.0"

[[requires]]
api = "core.io"
version = "~2.1"
```

The package manager resolves dependencies **by API contract**, not by filename.  
This allows multiple package versions to coexist if they expose compatible interfaces.

---

## 6.1 System vs Embedded Dependencies

Not all dependencies should be packaged inside `.epk` archives.  
EXOS distinguishes between **system-level shared components** and **application-level dependencies**.

### **1. System Dependencies**
System dependencies are global components that provide hardware access or critical OS services.

| Category | Example | Provided By | Shared Across Packages |
|-----------|----------|--------------|------------------------|
| GPU drivers | `libcuda.so`, `libvulkan.so` | `/library/package/driver.cuda.epk` | Yes |
| Audio drivers | `libalsa.so`, `libaudio.so` | `/library/package/driver.audio.epk` | Yes |
| Kernel runtime | `libc.so`, `libm.so`, core runtime | `/library/package/core.runtime.epk` | Yes |
| Network stack | `libnet.so`, sockets layer | `/library/package/netstack.epk` | Yes |

These components are considered *stable and trusted*, maintained by the OS vendor.  
They are mounted globally and accessible to all packages through `/library/package/<package-name>/`.

### **2. Embedded Dependencies**
Application-level libraries are packaged inside each `.epk` to ensure version independence and reproducibility.

| Category | Example | Packaged With |
|-----------|----------|---------------|
| GUI / framework | `libQt5Core.so`, `libSDL2.so` | Application |
| Codec / helper libs | `libpng.so`, `libjpeg.so`, `libjson.so` | Application |
| Engine or tool-specific code | Rendering modules, plugin loaders | Application |

These libraries live entirely inside the package and are not exposed globally.  
This prevents dependency hell and guarantees that each application uses exactly the environment it was built for.

### **3. Manifest-based Linking**

Each package manifest declares explicitly which shared system APIs it needs:

```toml
[[requires]]
api = "system.cuda"
version = ">=1.0"

[[requires]]
api = "system.opengl"
version = ">=4.0"

[[requires]]
api = "core.runtime"
version = "~2.1"
```

When mounting, the `packagefs` resolves those entries against the global package set.  
If a required system package is missing, the mount fails gracefully, allowing the userland manager to fetch the dependency.

### **4. Summary**

| Type | Location | Managed By | Update Model | Example |
|------|-----------|-------------|---------------|----------|
| System dependency | `/library/package/` | OS vendor | global updates | `driver.cuda.epk` |
| Shared runtime | `/library/package/` | OS core | versioned snapshots | `core.runtime.epk` |
| App-level lib | Inside app `.epk` | App developer | self-contained | `game.engine.epk` |

This hybrid approach provides both immutability and efficiency:
- Large, hardware-specific components are shared system-wide.  
- Smaller, version-sensitive libraries are isolated per package.


## 7. Updates and Rollback

- **Updating** = replacing one `.epk` file.
- **Rollback** = switching back to the previous package set (recorded snapshot).
- The system maintains a list of active package hashes:
  ```
  /system-data/package/active.list
  ```
  Booting from a previous state is instant and atomic (no reinstallation).

---

## 8. Advantages Summary

| Feature | Description |
|----------|--------------|
| Immutability | Packages are read-only, preventing corruption |
| Atomicity | Add/remove = one file operation |
| Reproducibility | Full system state = list of package files |
| Efficiency | zlib compression + VM page caching |
| Stability | No dependency hell; versioned contracts |
| Security | Optional package signatures and checksums |

---

## 9. Future Extensions

- **Delta packages** (binary diffs between versions)  
- **User namespaces** for sandboxed apps  
- **Optional encryption** of package contents  
- **Shared page deduplication** between similar packages  

---

## 10. Summary

The EXOS packaging model replaces the traditional installer paradigm with a **declarative, mount-based package system**.  
It prioritizes consistency, simplicity, and safety over raw I/O speed.  
Every system state is a reproducible composition of packages -- compressed, verified, and mounted dynamically.

This model eliminates classically fragile layers (installers, dependency solvers, post-install scripts) and provides a clean foundation for a stable, modular operating system.


---

## 11. Concrete Examples (Disk and Mounted Views)

### Example: Per-user package location

```
/users/alice/
|-- package/
|   `-- theme.dark.epk
|-- documents/
`-- settings/
```

### Example: Mounted view of global libraries

```
/library/package/
|-- core.runtime/
|   |-- binary/
|   |   |-- core.runtime.so
|   |   `-- image.codec.so
|   `-- manifest.toml
|-- driver.audio/
|   |-- binary/
|   |   `-- driver.audio.so
|   `-- manifest.toml
```

### Example: Mounted view of the application package (per-process)

```
/package/
|-- binary/
|   |-- video-editor
|   |-- video.engine.so
|   `-- image.codec.so
|-- data/
|   `-- presets/
|-- manifest.toml
```
Library selection: the loader resolves `/package/binary/image.codec.so` before `/library/package/core.runtime/binary/image.codec.so`.
