# AI agents guidance

## Project overview
This is a 32 bit operating system for i386-i686.

## Documentation
The documentation for boot sequence and kernel modules is in documentation/internal/Kernel.md

## Build
- run "make"

## Test
- run "./scripts/6-1-start-qemu-hd-nogfx.sh".
- check "log/debug.log" for page faults, exceptions, etc...

## General Conventions
- Use PascalCase for struct/class members and variables.
- Use SCREAMING_SNAKE_CASE for structure names and defines.
- Use hexadecimal for constant numbers, except for graphic points/sizes.
- Always use 4 spaces for indentation in code.
- Always write comments, console output and technical doc in english.
- There should be no duplicate code. Create intermediate functions to avoid it.

## Forbidden Actions
- Never enable network during tests.
- DON'T DELETE blank lines between functions, comments, etc...
- DON'T write python code, use JS (Node)
