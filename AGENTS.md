# Codex guidance

## Project overview
This is a 32 bit operating system for i386-i686.

## Documentation
The documentation for boot sequence and kernel modules is in documentation/internal/Kernel.md

## Build
make

## Test
- DON'T do any tests, there is no way you can test this OS for now.

## General Conventions
- Always use 4 spaces for indentation in code.
- Always write comments, console output and technical doc in english.
- There should be no duplicate code. Create intermediate functions to avoid it.

## Forbidden Actions
- Never enable network during tests.
- DON'T DELETE blank lines between functions, comments, etc...
- DON'T write python code
