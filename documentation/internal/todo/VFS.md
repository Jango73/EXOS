# Virtual File System Plan

## Goals
- Provide a Unix-like view of kernel objects through a hierarchical path.
- Allow any object to be mounted anywhere in the VFS tree.
- Expose block and character devices (e.g. disks, mouse, keyboard) as addressable objects.
- Keep architecture changes minimal while remaining consistent with existing kernel data structures.

## Object Model
- Introduce a base `OBJECT` structure inheriting from `LIST` via `LISTNODE_FIELDS`.
- `KERNELDATA` maintains a global list of `OBJECT` instances.
- Specialized objects derive from `OBJECT`:
    - `BLOCKDEVICE` for random-access storage.
    - `PHYSICALDISK` derives from `BLOCKDEVICE`.
    - `CHARDEVICE` for stream-oriented devices.
    - Mouse and keyboard derive from `CHARDEVICE` and appear in the VFS.

## Path Interpreter
- Implement kernel-level functions (e.g. in `Kernel.c` or a helper module) to resolve paths.
- `LocateObject(path)` splits the path and traverses mounted objects:
    - The first component selects an object registered at the root (e.g. `disk`).
    - The next component matches a child object (e.g. `ata0` within the `PHYSICALDISK` list).
    - Remaining components are delegated to the object's driver, returning items like `FILE`.
- `OpenObject`, `ListObjects` and similar helpers use the same interpreter.
- All callers, including the shell, pass an absolute or relative path string.

## Mounting
- Provide `MountObject(parentPath, childObject)` to insert any object into the VFS tree.
- Drivers register their root objects (disks, character devices) during initialization.
- Not every object supports random access; capability flags or interfaces indicate supported operations.

## File Listing
- `ListObjects(path)` returns children of the object resolved by `LocateObject`.
- Works for any mount point, enabling generic directory listing.

## Shell Integration
- Shell tracks only the current directory string (e.g. `/disk/ata0/work`).
- For operations (open, list, etc.), the shell builds a path and invokes the interpreter.
- `GetCurrentFileSystem` is removed; path resolution is centralized.

## Steps to Implement
1. Define `OBJECT`, `BLOCKDEVICE` and `CHARDEVICE` structures inheriting from `LISTNODE_FIELDS`.
2. Create kernel functions for registering objects and mounting them into the VFS.
3. Implement path parsing and traversal (`LocateObject`, `OpenObject`, `ListObjects`).
4. Update drivers to expose their objects via the new model.
5. Modify `Shell.c` to rely solely on path strings.
6. Add mouse and keyboard as `CHARDEVICE` instances visible in the VFS.
7. Document behaviors and edge cases (permissions, error codes) for future extension.
