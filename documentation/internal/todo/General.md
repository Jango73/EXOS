# General TODO list

## Fixes

- NetworkManager.c:73 : config file (exos.toml) IS able to handle per device settings.

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

- Kernel pointer masking
- NX/DEP
- PIE/ASLR userland
- Stack canaries
- RELRO
- Signed kernel modules + Secure Boot
- KASLR
- Audit/fuzz pipeline + ASAN/UBSAN

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

## 