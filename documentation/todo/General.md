# General TODO list

## Logs

- Use __func__ to automatically include function name

## Init and shutdown

- All modules called by InitializeKernel expose an interface similar to drivers.
- This interface implements Initialize() and Shutdown() : Initialize() must be idempotent.
- The exposed interfaces are listed in a kernel object.
- There is a inter-module dependency mecanism : what modules does a module depend on regarding initialization. Therefore, initialization order can be automatically determined by the kernel.

## Errors

- Functions returning U32 MUST return DF_ERROR_XXXX codes : meaning 0 on success, an error otherwise.
- If they are meant to return 0 or 1, they must use BOOL.

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

## Multitasking

- Improve the scheduler (task priorities)

## File systems

- Implement ext2, ext3, ext4

## Drivers

- USB
- PCIe
- NVMe

## Localization

- Unicode
- I18n

## Desktop

- Continue graphics UI

## Other

- Add quotes at startup
