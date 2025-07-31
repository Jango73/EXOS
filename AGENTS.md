# Codex guidance

## Project overview
This is a 32 bit operating system for i386.

## Build
make

## Test
- DON'T do any tests, there is no way you can test this OS for now.

## General Conventions
- Always use 4 spaces for indentation in code.
- Prefer array methods over manual loops.
- Always write comments, console output and technical doc in english.
- There should be no duplicate code. Create small functions to avoid it.

## Forbidden Actions
- Never enable network during tests.
- DON'T DELETE blank lines between functions, blocks of code, etc...

## Boot sequence
When launching the DOS loader, the sequence is

- Loader.asm : Main (loads exos.bin and jumps to its base)
- Stub.asm : StartAbsolute (does nothing, just a jump)
- Stub.asm : Start (loads IDT & GDT and switches to PM)
- Stub.asm : Start32 (gets RAM size, sets up page directory and tables, enables paging)
- System.asm : ProtectedModeEntry (fixes segments, stores stub info, sets up stack)
- Main.c : KernelMain()
- Kernel.c : InitializeKernel()
