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

Conceptually, the package manager is not an “installer” — it’s a **volume orchestrator**.

---

## 2. Design Principles

1. **Mount, not copy**  
   Packages remain compressed archives (`.hpkg`) and are mounted by the kernel’s *packagefs* rather than unpacked.

2. **Read-only system base**  
   The core system runs entirely from mounted packages.  
   No writable files exist in `/system`; user and runtime data live in `/data`.

3. **Atomic state changes**  
   Installing or removing a package = adding or removing a single file.  
   The kernel rebuilds the active package graph in real-time.

4. **Layered namespaces**  
   Packages are overlaid in priority order:
   ```
   /system/   → core system packages
   /common/   → shared optional packages
   /user/     → per-user packages and overlays
   ```
   This allows user-level package customization without affecting the base OS.

5. **Compression transparency**  
   Files inside packages are zlib-compressed and transparently decompressed on demand.  
   Executables and libraries are never extracted to disk; they are streamed directly into memory at load time.

6. **Minimal external tooling**  
   Package management requires only:
   - filesystem support for `packagefs`,
   - a userland tool for adding/removing `.hpkg` files,
   - a daemon (optional) for dependency resolution.

---

## 3. Package Structure

Each package (`.hpkg`) is a **single compressed archive** with an internal directory tree and metadata table.
The format is described here : https://www.haiku-os.org/docs/develop/packages/FileFormat.html

---

## 4. Package Mounting Process

1. **Detection**  
   At boot or runtime, the system scans known package directories:
   ```
   /system/packages/
   /common/packages/
   /user/packages/
   ```

2. **Validation**  
   Each `.hpkg` file is verified (header, checksum, optional signature).

3. **Mounting**  
   The kernel mounts the package into a virtual view (like a read-only overlay).  
   Files inside the package become visible in the global namespace:
   ```
   /system/packages/core.hpkg → /system/bin, /system/lib, /system/include
   ```

4. **On-demand decompression**  
   When a file is accessed, the filesystem reads its compressed blocks from the package and decompresses them in memory.  
   Pages are cached transparently by the kernel’s VM.

5. **Unloading**  
   Removing a package simply unmounts it; no residual files remain.

---

## 5. Execution Model

- **Binaries and Libraries**  
  When the ELF loader requests an executable or `.so`, the `packagefs` supplies the decompressed bytes directly to memory.
  No temporary files are created.

- **Caching**  
  Frequently used pages are cached in RAM for instant reuse.  
  An optional `/var/cache/exec/` can persist decompressed code across boots.

- **Isolation**  
  Each process inherits a view of the mounted package graph according to its namespace.  
  System processes see `/system + /common`, while user processes may overlay `/user`.

---

## 6. Dependency and Version Management

Each package includes a `manifest.json` file describing:
```json
{
  "name": "core.graphics",
  "version": "2.4.1",
  "provides": ["api.graphics", "api.window"],
  "requires": [
    { "api": "core.kernel", "version": ">=1.0" },
    { "api": "core.io", "version": "~2.1" }
  ]
}
```

The package manager resolves dependencies **by API contract**, not by filename.  
This allows multiple package versions to coexist if they expose compatible interfaces.

---


---

## 6.1 System vs Embedded Dependencies

Not all dependencies should be packaged inside `.hpkg` archives.  
EXOS distinguishes between **system-level shared components** and **application-level dependencies**.

### **1. System Dependencies**
System dependencies are global components that provide hardware access or critical OS services.

| Category | Example | Provided By | Shared Across Packages |
|-----------|----------|--------------|------------------------|
| GPU drivers | `libcuda.so`, `libvulkan.so` | `/system/packages/driver.cuda.hpkg` | Yes |
| Audio drivers | `libalsa.so`, `libaudio.so` | `/system/packages/driver.audio.hpkg` | Yes |
| Kernel runtime | `libc.so`, `libm.so`, core runtime | `/system/packages/core.runtime.hpkg` | Yes |
| Network stack | `libnet.so`, sockets layer | `/system/packages/netstack.hpkg` | Yes |

These components are considered *stable and trusted*, maintained by the OS vendor.  
They are mounted globally and accessible to all packages through `/system/lib/`.

### **2. Embedded Dependencies**
Application-level libraries are packaged inside each `.hpkg` to ensure version independence and reproducibility.

| Category | Example | Packaged With |
|-----------|----------|---------------|
| GUI / framework | `libQt5Core.so`, `libSDL2.so` | Application |
| Codec / helper libs | `libpng.so`, `libjpeg.so`, `libjson.so` | Application |
| Engine or tool-specific code | Rendering modules, plugin loaders | Application |

These libraries live entirely inside the package and are not exposed globally.  
This prevents “dependency hell” and guarantees that each application uses exactly the environment it was built for.

### **3. Manifest-based Linking**

Each package manifest declares explicitly which shared system APIs it needs:

```json
"uses": [
  "system.cuda",
  "system.opengl",
  "core.runtime"
]
```

When mounting, the `packagefs` resolves those entries against the global package set.  
If a required system package is missing, the mount fails gracefully, allowing the userland manager to fetch the dependency.

### **4. Summary**

| Type | Location | Managed By | Update Model | Example |
|------|-----------|-------------|---------------|----------|
| System dependency | `/system/packages/` | OS vendor | global updates | `driver.cuda.hpkg` |
| Shared runtime | `/system/packages/` | OS core | versioned snapshots | `core.runtime.hpkg` |
| App-level lib | Inside app `.hpkg` | App developer | self-contained | `game.engine.hpkg` |

This hybrid approach provides both immutability and efficiency:
- Large, hardware-specific components are shared system-wide.  
- Smaller, version-sensitive libraries are isolated per package.


## 7. Updates and Rollback

- **Updating** = replacing one `.hpkg` file.
- **Rollback** = switching back to the previous package set (recorded snapshot).
- The system maintains a list of active package hashes:
  ```
  /system/state/active.list
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
- **Runtime package overlays** (for debugging or hot-patching)  
- **User namespaces** for sandboxed apps  
- **Optional encryption** of package contents  
- **Shared page deduplication** between similar packages  

---

## 10. Summary

The EXOS packaging model replaces the traditional installer paradigm with a **declarative, mount-based package system**.  
It prioritizes consistency, simplicity, and safety over raw I/O speed.  
Every system state is a reproducible composition of packages — compressed, verified, and mounted dynamically.

This model eliminates classically fragile layers (installers, dependency solvers, post-install scripts) and provides a clean foundation for a stable, modular operating system.
