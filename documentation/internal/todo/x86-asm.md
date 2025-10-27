# Plan for x86-64 Assembly/Disassembly Support

## 1. Assess current i386 assembler/disassembler
- [ ] Review `kernel/source/arch/intel/i386-Asm.c` to catalog existing opcode handlers, data structures, and helper utilities.
- [ ] Identify assumptions limited to 32-bit operands, registers, or addressing modes that must be extended for 64-bit.
- [ ] Document reusable routines that can directly serve the x86-64 implementation (tokenizer, encoder, decoder, etc.).

## 2. Decide integration strategy
- [ ] Determine if x86-64 logic can coexist inside `i386-Asm.c` without harming maintainability:
  - Evaluate feasibility of adding 64-bit conditionals guarded by ISA mode flags.
  - Estimate the additional complexity for opcode tables and operand resolution.
- [ ] If integration bloats the file, outline separation into a dedicated `x86-64-Asm.c` that wraps existing 16/32-bit helpers.
  - Define the public interface each file should expose (assemble/disassemble entry points, shared structs).

## 3. Shared components and abstractions
- [ ] Extract common definitions (token enums, operand descriptors, prefix handling) into a shared header/source pair (`intel-AsmCommon.h/.c`).
- [ ] Ensure 0x66 operand-size override handling is centralized so both i386 and x86-64 paths reuse the same logic.
- [ ] Provide utility functions for register tables that map between textual names and register IDs for 8/16/32/64-bit variants.

## 4. x86-64 specific assembly support
- [ ] Extend operand parsing to recognize REX prefixes, new registers (R8-R15), and 64-bit immediate forms.
- [ ] Implement encoding rules for 64-bit instructions, leveraging existing i386 encoding where unchanged.
- [ ] Add tests/fixtures demonstrating assembly of representative 64-bit instructions (integer ALU, control flow, syscalls).

## 5. x86-64 disassembly support
- [ ] Update the decoder to interpret REX prefixes, 64-bit ModRM extensions, and new opcode maps.
- [ ] Implement pretty-printing logic for 64-bit registers and immediates, reusing i386 formatting helpers.
- [ ] Ensure operand-size overrides (0x66) delegate to the shared i386 routines when decoding legacy forms.

## 6. Integration and verification
- [ ] Define build targets to compile the new source file if `x86-64-Asm.c` is introduced.
- [ ] Update any assembler/disassembler front-ends or CLI tools to select the correct backend based on architecture.
- [ ] Create regression tests comparing encoded bytes vs. disassembly round-trips for mixed 32/64-bit cases.
- [ ] Document the final architecture decisions in `documentation/internal/Kernel.md` once implementation lands.
