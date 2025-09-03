# AI agents guidance

## Project overview
This is a 32 bit operating system for i386-i686.

## Documentation
The documentation for boot sequence and kernel modules is in documentation/internal/Kernel.md.
Kernel.md must be updated when adding/removing a file/class/function.

## Build
- Run scripts/4-4-build.sh to test without debug information.
- Run scripts/4-5-build-debug.sh to test with standard debug information.
- Run scripts/4-6-build-scheduling-debug.sh to test with standard and scheduling debug information (flood of debug info).

If testing Clock, Scheduler, Interrupts mechanism, build with scripts/4-6-build-scheduling-debug.sh.

## Test (Codex : don't bother, you don't have the tools)
- Run 5-1-start-qemu-hd.sh if a display is available, 6-1-start-qemu-hd-nogfx.sh otherwise.
- Wait a moment for system to initialize, takes longer if scheduling debug info is dumped.
- Read "log/debug-com2.log" for page faults, exceptions, etc...

## General Conventions
- Use full names for struct/class members and variables, or acronyms (no "len", "sz", "idx", ...).
- Use PascalCase for struct/class members and variables.
- Use SCREAMING_SNAKE_CASE for structure names and defines.
- Use hexadecimal for constant numbers, except for graphic points/sizes and time.
- Use 4 spaces for indentation in code.
- Write comments, console output and technical doc in english.
- There should be no duplicate code. Create intermediate functions to avoid it.

## Format
- Format must follow clang-format rules in .clang-format

## Forbidden Actions
- Never enable network during tests.
- DON'T delete blank lines between functions, comments, etc...
- DON'T write python code, use JS (Node)
- DON'T use any stdlib, stdio in the kernel, it does not exist.
