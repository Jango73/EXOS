# Native C Compiler in EXOS

## Goal

Provide native C compilation inside EXOS with the smallest viable implementation effort.

## Recommended Path

Port TinyCC as a userland EXOS application and run it in static executable mode first.

Rationale:
- TinyCC is significantly smaller to port than GCC or Clang.
- TinyCC can keep compilation and final linking in one program.
- This matches the existing EXOS executable loading path (ET_EXEC ELF, no PT_INTERP).

## Existing EXOS Capabilities to Reuse

- Userland file I/O and process launch are available through runtime wrappers:
  - `runtime/source/exos-runtime-c.c`
- Userland heap allocation is available (`malloc`, `free`, `realloc`):
  - `runtime/source/exos-runtime-c.c`
- Argument parsing for user processes is available (`_SetupArguments`, `_argc`, `_argv`):
  - `runtime/source/exos-runtime-c.c`
- Kernel exposes file, process, memory and console syscalls:
  - `kernel/include/User.h`
  - `kernel/source/SYSCallTable.c`
- ELF loader already validates executable format and rejects dynamic interpreter usage:
  - `kernel/source/ExecutableELF.c`
- User application link layout is defined in:
  - `runtime/make/exos.ld`
  - `runtime/make/exos.mk`

## Gaps to Close First

1. Userland C API Completeness
- Add missing runtime functions commonly required by a compiler frontend/runtime:
  - numeric conversion helpers (`strtol`, `strtoul`)
  - character classification helpers (`ctype` family)
  - sorting/search helpers (`qsort` at minimum)
  - basic error reporting model (`errno`)
- Add file operations expected by build tools:
  - `remove`, `rename`
  - optional: `stat` equivalent if required by the selected TinyCC features

2. Header Set for Userland
- Consolidate a coherent userland-facing header set used by native tools and user applications.
- Avoid relying on kernel stubs under `third/include` for native tool builds.

3. Tool Runtime Packaging
- Provide runtime objects and libraries required for final application link in EXOS format.
- Keep the output aligned with EXOS loader expectations (static ET_EXEC path).

## Implementation Phases

## Phase 1 - Bootstrap TinyCC Program

- Add `system/tcc` application target.
- Cross-build TinyCC source into an EXOS executable.
- Validate execution in EXOS:
  - `tcc -v`
  - simple parser-only checks

Acceptance criteria:
- `tcc` starts in EXOS and reads files from filesystem.
- `tcc` can parse command line arguments and report diagnostics to console.

## Phase 2 - Runtime Compatibility Layer

- Implement minimal compatibility glue in EXOS runtime for TinyCC required calls.
- Keep glue reusable and isolated (no duplicated one-off logic inside the app).
- Add focused tests where runtime functions are extended.

Acceptance criteria:
- `tcc -c hello.c -o hello.o` works in EXOS.
- Generated object format is consistent for the selected architecture.

## Phase 3 - Native Final Link

- Enable TinyCC final executable output directly for EXOS user applications.
- Ensure output ELF properties are accepted by EXOS loader:
  - executable type: ET_EXEC
  - loadable segments: PT_LOAD
  - no dynamic interpreter segment (no PT_INTERP)

Acceptance criteria:
- `tcc hello.c -o hello` produces an EXOS runnable program.
- Produced binary starts and exits correctly under EXOS.

## Phase 4 - Multi-File and Library Workflow

- Support compilation units and archives in practical workflows:
  - multiple `.c` files
  - static libraries when needed
- Validate linkage against EXOS runtime library.

Acceptance criteria:
- Build a non-trivial multi-file sample entirely inside EXOS.
- Repeatable build result without manual post-processing.

## Phase 5 - Quality and Hardening

- Add diagnostics and limits for large sources.
- Validate memory behavior under constrained heap.
- Add regression tests for compile, link and run.

Acceptance criteria:
- Native compile flow passes repeatable smoke tests.
- Failure paths produce actionable diagnostics.

## Scope Control

Start with one architecture (x86-32) for first native compiler milestone.
Only expand to x86-64 after stable end-to-end success on x86-32.

Do not target dynamic linking in the initial milestones.
Do not target full POSIX compatibility in the initial milestones.

## Deliverables

1. `system/tcc` native compiler executable.
2. Runtime compatibility extensions required by the compiler.
3. Native documentation for compile workflows and limits.
4. Smoke tests that compile and execute at least one C program entirely inside EXOS.
