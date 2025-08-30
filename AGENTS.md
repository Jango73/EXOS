# AI agents guidance

## Project overview
This is a 32 bit operating system for i386-i686.

## Documentation
The documentation for boot sequence and kernel modules is in documentation/internal/Kernel.md

## Build
- run "make"

## Test (Codex : don't bother, you don't have the tools)
- run "./scripts/6-1-start-qemu-hd-nogfx.sh".
- check "log/debug.log" for page faults, exceptions, etc...

## General Conventions
- Use full names for struct/class members and variables, or acronyms (no "len", "sz", "idx", ...).
- Use PascalCase for struct/class members and variables.
- Use SCREAMING_SNAKE_CASE for structure names and defines.
- Use hexadecimal for constant numbers, except for graphic points/sizes and time.
- Use 4 spaces for indentation in code.
- Write comments, console output and technical doc in english.
- There should be no duplicate code. Create intermediate functions to avoid it.

## Forbidden Actions
- Never enable network during tests.
- DON'T delete blank lines between functions, comments, etc...
- DON'T write python code, use JS (Node)
- DON'T use any stdlib, stdio in the kernel, it does not exist.
