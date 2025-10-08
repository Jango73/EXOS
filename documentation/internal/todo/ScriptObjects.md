# Script Engine Object Exposure Plan

## Goals
- Introduce host-defined objects to the scripting runtime without hard dependencies between `Script.c` and shell-specific structures.
- Allow scripts to access host properties (including nested objects and arrays) via qualified identifiers such as `process[5].path`.
- Ensure host-exposed identifiers shadow script-defined variables while remaining globally scoped.
- Maintain the existing one-way architecture: the script engine interacts with opaque host handles exclusively through callbacks provided during registration.

## Architectural Principles
1. **Host Abstraction**
   - `Script.c` must treat every host object as an opaque handle. No struct definitions or direct knowledge of host modules may leak into the interpreter.
   - The host provides callback tables describing how to retrieve elements and properties; `Script.c` stores and forwards these pointers without dereferencing host types.
2. **Callback Ownership**
   - All callbacks invoked by the script engine must be registered up front (during interpreter initialization or via dedicated registration API).
   - Callbacks execute in host context and return primitive script values or further opaque handles representing nested objects.
3. **Name Resolution Priority**
   - Identifier lookup must check the host registry before consulting script scopes.
   - Host-defined identifiers live in a dedicated global table to avoid scope pollution while preserving fast lookups.
4. **Value Semantics**
   - Script variables retain existing semantics; only host-backed identifiers bypass the script storage layer.
   - Qualified access (e.g., `object.property`) is resolved dynamically through host callbacks with caching kept optional.

## Data Structure Updates
1. **Host Identifier Registry**
   - Create a new registry structure (e.g., `SCRIPT_HOST_SYMBOL`) capturing:
     - Public name (`const char *` stored/duplicated by host or script engine depending on ownership rules).
     - Symbol kind (scalar property, array entry point, object handle, etc.).
     - Pointer to the primary callback (`ScriptHostSymbolCallback`) and optional metadata (context pointer, capabilities flags).
   - Add a lookup map accessible from `Script.c` during parsing/execution; consider reusing existing dictionary utilities if available.
2. **Opaque Host Handle Type**
   - Define a generic handle (e.g., `typedef void *ScriptHostHandle;`) to store values returned by host callbacks representing objects.
   - Extend `SCRIPT_VALUE` or equivalent union to carry a host-handle variant, with appropriate reference semantics.
3. **Qualified Identifier Nodes**
   - Extend the AST or bytecode instruction set to encode qualified access (`identifier '.' property`) and indexed host accesses (`identifier '[' expr ']'`).
   - Nodes must distinguish between script arrays and host arrays so the VM dispatches to the correct evaluation path.
4. **Callback Descriptors**
   - Introduce descriptor structs capturing arrays of callbacks per object type:
     - Element accessor for indexed lookup.
     - Property accessor for named lookup.
     - Optional enumeration callbacks if iteration is supported.
   - Ensure descriptors are registered per object class and referenced by host handles via an identifier or direct pointer.

## API Extensions Between Host and Script Engine
1. **Registration Functions**
   - Add APIs such as `ScriptRegisterHostObject` and `ScriptRegisterHostArray` that the host calls to expose data.
   - Parameters include symbol name, callback descriptor, optional context pointer, and lifetime flags.
2. **Callback Signatures**
   - Define canonical signatures:
     - `ScriptHostGetElement(void *Context, ScriptHostHandle Parent, uint32_t Index, SCRIPT_VALUE *OutValue)`.
     - `ScriptHostGetProperty(void *Context, ScriptHostHandle Parent, const char *Property, SCRIPT_VALUE *OutValue)`.
   - Ensure callbacks return status codes understood by `Script.c` (e.g., success, missing member, type mismatch).
3. **Error Propagation**
   - Standardize error codes for host callback failures and propagate them into script runtime errors with descriptive messages.
4. **Lifetime Management**
   - Clarify ownership of handles returned by callbacks (e.g., host-managed, script must call release, or handles are immutable snapshots).
   - If needed, add optional release callback invoked when the script engine discards a handle.

## Interpreter Changes
1. **Identifier Resolution Path**
   - Modify the lookup routine to query the host registry first.
   - When a host symbol is found, emit instructions that invoke host callbacks at runtime instead of binding to script storage.
2. **Execution Engine**
   - Extend the VM to support new opcode(s) that:
     - Fetch array elements through host callbacks (`HOST_GET_INDEX`).
     - Fetch properties from host handles (`HOST_GET_PROPERTY`).
   - Ensure evaluation order matches existing script semantics (left-to-right, index evaluated before callback invocation, etc.).
3. **Scope Handling**
   - Host symbols are inherently global; ensure they bypass local scope push/pop logic.
   - Prevent scripts from shadowing host symbols by name, possibly by erroring during script variable declaration if a conflict is detected.
4. **Type Enforcement**
   - Guard runtime conversions so host callbacks returning primitive values integrate seamlessly with script arithmetic/string operations.
   - When receiving a host handle in a context expecting a primitive, raise descriptive runtime errors.

## Parser Adjustments
1. **Qualified Identifiers**
   - Update grammar to recognize `identifier '.' identifier` and `identifier '[' expression ']'` combinations that resolve to host object access.
   - Distinguish between host and script arrays by consulting the symbol table during semantic analysis.
2. **Error Messages**
   - Provide explicit diagnostics when a script tries to use host-only syntax on non-host values (e.g., `scriptVar.property`).

## Testing Strategy
1. **Unit Tests**
   - Create interpreter-level tests verifying host registration, lookup precedence, and property access.
   - Mock host callbacks returning predictable values to validate flow without relying on shell internals.
2. **Integration Tests**
   - From `Shell.c`, expose sample process arrays and validate script expressions like `process[1].path` or nested properties.
3. **Regression Tests**
   - Ensure legacy scripts without host objects continue to function unchanged.

## Documentation & Follow-Up Work
- Document the new API in `documentation/internal/Project.md` and relevant header comments once implemented.
- Provide examples of host registration in `Shell.c` after the interpreter changes land.
- Consider future enhancements such as host-driven iteration, property enumeration, and caching strategies for frequently accessed properties.
