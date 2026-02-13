# Kernel Logical Path Configuration Plan

## Objective

Allow the kernel to resolve logical path names from configuration, so components do not rely on hardcoded absolute VFS paths.

Example goal:
- Instead of always reading the user database from a fixed path `x`,
- allow configuration to redirect it to path `y`.

This complements `SystemFS.Mount`, which maps physical filesystem folders into VFS nodes.

## Current State (Codebase Analysis)

- User mounts are loaded from `SystemFS.Mount` in `kernel/source/SystemFS.c` (`MountUserNodes()`).
- Kernel configuration is loaded before mount usage in `kernel/source/FileSystem.c` (`ReadKernelConfiguration()` then `MountUserNodes()`).
- Hardcoded kernel paths identified:
  - User database path constant (`PATH_USERS_DATABASE`) in `kernel/include/utils/Helpers.h`, used by:
    - `kernel/source/UserAccount.c` (`LoadUserDatabase()`)
    - `kernel/source/UserAccount.c` (`SaveUserDatabase()`)
  - Keyboard layout folder `"/system/keyboard/"` in:
    - `kernel/source/Key.c` (`UseKeyboardLayout()`)

## Proposed Architecture

Introduce a reusable kernel path resolver module:

- Header: `kernel/include/utils/KernelPath.h`
- Source: `kernel/source/utils/KernelPath.c`

This module resolves logical path names from TOML configuration with strict fallback behavior.

## Configuration Format

Add a dedicated section in `exos.ref.toml`:

```toml
[KernelPath]
UsersDatabase="/system/data/users.database"
KeyboardLayouts="/system/keyboard"
```

Notes:
- Values are absolute VFS paths.
- Missing keys keep default behavior.

## API Proposal

```c
BOOL KernelPathResolve(
    LPCSTR Name,
    LPCSTR DefaultPath,
    LPSTR OutPath,
    UINT OutPathSize);

BOOL KernelPathBuildFile(
    LPCSTR FolderName,
    LPCSTR DefaultFolder,
    LPCSTR Leaf,
    LPCSTR Extension,
    LPSTR OutPath,
    UINT OutPathSize);
```

Behavior:
- Read `KernelPath.<Name>` from loaded TOML config.
- Validate configured path.
- If valid: return configured path.
- If invalid or missing: log warning (if invalid) and return default path.

## Validation Rules

Configured path must:
- be non-empty,
- be absolute (`/` prefix),
- fit in `MAX_PATH_NAME`,
- preserve safe output buffer limits.

On invalid value:
- keep execution safe,
- use default path,
- emit a warning with function-prefixed log format.

## Migration Plan

1. Implement `KernelPath` module in `kernel/include/utils` + `kernel/source/utils`.
2. Replace user database path usage:
   - `UserAccount.c`: replace direct `PATH_USERS_DATABASE` callsites with `KernelPathResolve("UsersDatabase", ...)`.
3. Replace keyboard layout folder usage:
   - `Key.c`: replace hardcoded `"/system/keyboard/"` with `KernelPathBuildFile("KeyboardLayouts", ...)`.
4. Keep defaults unchanged when `[KernelPath]` section is absent.
5. Update kernel documentation:
   - `documentation/Kernel.md` with the new `KernelPath` mechanism and keys.

## Compatibility and Risk

- Backward compatible by design (fallback to defaults).
- No behavior change for existing configurations that do not define `[KernelPath]`.
- Improves maintainability by avoiding repeated local path logic in subsystems.
