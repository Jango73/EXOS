# General TODO list

## Memory

- Later: align x86-32 page directory creation (`AllocPageDirectory` and `AllocUserPageDirectory`) with the modular x86-64 region-based approach (low region, kernel region, task runner, recursive slot) while preserving current behavior. Execute this refactor in small validated steps to limit boot and paging regression risk.

## Problems

- Opening a file in a userland program without an absolute path should do the same as using getcwd().
- Add a getpd() that returns the folder in which the current executable's image lives.

## System data view

- Add following infos in PCI page:
  - VendorID/DeviceID
  - Command / Status
  - Class/Subclass/ProgIF
  - BAR0..BAR5 (detect 32 vs 64-bit)
  - Capabilities Pointer + scan capabilities (MSI/MSI-X, PCIe)
  - Interrupt Line/Pin

## Naming

- Remove all abbreviations

## Logs

- Use __func__ to automatically include function name

## Errors

- Functions returning U32 MUST return DF_RETURN_XXXX codes : meaning 0 on success, an error otherwise.
- If they are meant to return 0 or 1, they must use BOOL.

## Shell kernel objects exposure

## Scripting

- Use + for string concat

## Shared modules

- Load and map shared modules

## Filesystem cache

- Create a generic fixed-size cache
- Add a cluster cache to FAT16 and FAT32

## Network
- Optimize/evolve the network stack

## Keyboard

- Add more keyboard layouts

## Security 

- NX/DEP : Prevents execution in non-executable memory regions (stack/heap), blocking classic injected shellcode attacks.
- PIE/ASLR userland : Makes userland binaries position-independent and randomizes memory layout to hinder return-oriented and memory-guessing attacks.
- Stack canaries : Places sentinel values before return addresses to detect and stop stack buffer overflows before control hijack.
- RELRO : Marks relocation tables read-only to stop attackers from modifying GOT/PLT entries at runtime.
- Signed kernel modules + Secure Boot : Allows only cryptographically signed kernel modules and verifies the boot chain to prevent unauthorized code from loading.
- KASLR : Randomizes the kernel's memory base to make kernel address offsets unpredictable for exploitation.
- Audit/fuzz pipeline + ASAN/UBSAN : Continuous auditing and fuzzing with sanitizers to catch memory errors and undefined behavior during development.

## Multicore

- Handle n CPUs

## Scheduling

- Improve the scheduler (task priorities)
- A CPU-bound task that never blocks can starve lower-priority deferred work and input handling (e.g., System Data View loop). Preference: force yield when a task is too CPU-hungry.

## File systems

- Implement ext3 and ext4
- Load exos.bin from the EXT2 system partition instead of the ESP in UEFI

## Drivers

- PCIe

## Localization

- UTF
- Unicode
- I18n

## Desktop

- Continue graphics UI

## Other

- Add quotes at startup
