# Plan for x86-64 Assembly/Disassembly Support

## 1. Assess current i386 assembler/disassembler
- ✅ **Opcode tables and instruction descriptors.** The 512-entry `Opcode_Table` and 80-entry `Extension_Table` live in
  `kernel/source/arch/intel/i386-Asm-Tables.c` and are exposed through `INTEL_OPCODE_PROTOTYPE` records declared in
  `kernel/include/arch/intel/i386-Asm.h`. The tables already cover 16/32-bit integer opcodes but intentionally exclude
  x87/MMX/SSE families, so any 64-bit port must either extend the tables or load a parallel set for long-mode opcodes.
- ✅ **Decoder/encoder core.** `Intel_MachineCodeToStructure()` performs prefix handling, ModR/M dispatch, and operand decoding
  using helpers such as `Intel_GetModR_M()`, `Intel_Decode_ModRM_Addressing_*()`, and `Intel_Decode_Operand()`.
  Reassembly flows through the mirrored `Intel_StructureToMachineCode()` routine. Both read/write paths rely on
  the `INTEL_INSTRUCTION` aggregate (mnemonic, ModR/M, operands, effective sizes) and the `INTEL_OPERAND_*` unions for
  typed operands. These abstractions are mature enough to reuse for x86-64 once register and size metadata are widened.
- ✅ **32-bit-centric assumptions.** Register metadata and operand formatting stop at the legacy set: `Intel_RegNames`
  enumerates AL–EDI, MM0–MM7, and segment/control registers only, and `Intel_GetRegisterSize()` reports 64-bit solely for
  MMX entries. Addressing helpers hard-code 32-bit bases (`INTEL_REG_EAX`…`INTEL_REG_EDI`) and never emit RIP-relative forms.
  Prefix handling recognises 0x66/0x67 toggles but has no awareness of REX, mandatory prefixes, or the default 64-bit operand
  size that long mode enforces. The encoder mirrors these gaps, for example by forcing PUSH on 32-bit registers to silently
  downshift to 16-bit encodings. These constraints must be lifted to accept R8–R15, 64-bit immediates, and the new addressing
  rules.
- ✅ **Reusable building blocks.** Tokenising/formatting helpers such as `Intel_PrintTypeSpec()` and
  `Intel_StructureToString()` already understand size-driven text like `BYTE PTR`/`QWORD PTR` and relative branches.
  They will remain valuable if register lookups and displacement calculations gain 64-bit variants. Similarly, the
  ModR/M/SIB decoders can be refactored to treat 16/32/64-bit flows via size-aware tables rather than duplicated logic.

## 2. Decide integration strategy
- ✅ **Single-file expansion analysis.** `i386-Asm.c` is already >1,600 lines combining decoding, encoding, formatting, and
  special cases. Threading 64-bit support via conditionals would require retrofitting every register table, ModR/M branch,
  and operand constructor to understand REX state and long-mode defaults. The risk of regressions in the well-tested 16/32-bit
  paths is high, and readability would further degrade because the current layout intermixes decoding, encoding, and utility
  layers without modular boundaries.
- ✅ **Recommended split.** Introduce a dedicated `x86-64-Asm.c` that:
  - Reuses shared infrastructure extracted to `intel-AsmCommon.{h,c}` (register metadata, prefix evaluation, opcode descriptor
    definitions) so legacy assemblers keep working unchanged.
  - Maintains a long-mode opcode table (mirroring the existing structures) plus thin wrappers around `Intel_MachineCodeTo*`
    once those functions accept injected size/prefix policies.
  - Exposes public entry points such as `Intel64_MachineCodeToStructure()` / `Intel64_StructureToMachineCode()` to mirror the
    32-bit interface, while sharing operand/structure types to maximise code reuse.
- ✅ **Rationale.** Keeping ISA-specific control flows in separate translation units limits the risk of mixing REX logic with
  legacy override handling, simplifies unit testing per architecture, and allows the 64-bit assembler to evolve (e.g., adding
  RIP-relative or syscall forms) without destabilising 16/32-bit consumers. Shared headers ensure the caller surface remains
  uniform regardless of the backend chosen.

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
- [ ] Document the final architecture decisions in `documentation/Kernel.md` once implementation lands.
