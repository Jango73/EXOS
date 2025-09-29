# Shell Scripting Integration Plan

## Overview
Plan to integrate minimal scripting capabilities into Shell.c for EXOS. This will enable variable management, basic arithmetic, control flow, and command blocks while maintaining the lightweight nature of the shell.

## Core Components to Implement

### 1. Variable Management System
- **Variable Storage**: Hash table for variable names and values
- **Variable Types**: String, integer and float support
- **Array Support**: Simple indexed arrays (e.g., `arr[0]`, `arr[1]`)
- **Memory Management**: Proper allocation/deallocation of variable storage
- **Scope Handling**: Global scope initially, local scope for blocks later

### 2. Expression Parser
- **Tokenizer**: Break input into tokens (identifiers, operators, literals, parentheses)
- **Expression Evaluator**: Recursive descent parser for mathematical expressions
- **Operator Precedence**: Handle precedence for `+`, `-`, `*`, `/`, parentheses
- **Comparison Operators**: Support for `<`, `<=`, `>`, `>=`, `==`
- **Assignment Operator**: Handle `=` for variable assignment
- **Parentheses Support**: Group expressions with `(` and `)`
- **End of statement**: Use `;` to mark end of statement

### 3. Command Block Parser
- **Block Delimiters**: Parse `{` and `}` for command grouping
- **Nested Blocks**: Support for nested block structures
- **Block Execution**: Sequential execution of commands within blocks
- **Variable scope**: Scope concept : a block (even the root block) accesses variables within its scope and parent scopes
- **Error Handling**: Proper error reporting for malformed blocks

### 4. Control Flow Structures

#### If Statements
- **Syntax**: `if (condition) { commands }`
- **Condition Evaluation**: Boolean result from comparison expressions
- **Command Execution**: Execute block only if condition is true
- **Else Support**: Optional `else` clause for alternative execution

#### For Loops
- **Syntax**: `for (init; condition; increment) { commands }`
- **Loop Variable**: Support for loop counter variables
- **Condition Check**: Evaluate condition before each iteration
- **Increment/Decrement**: Update loop variable after each iteration
- **Break/Continue**: Optional loop control statements

### 5. Integration Points

#### Shell.c Modifications
- **Input Parser Enhancement**: Extend existing command parser to handle scripting syntax
- **Command Execution**: Modify command execution to support control flow
- **Variable Resolution**: Replace variable references in commands with their values
- **Error Reporting**: Enhanced error messages for scripting syntax errors

#### Memory Integration
- **Heap Usage**: Use heap management for variable storage
- **Memory Safety**: Ensure proper cleanup of scripting resources
- **Resource Limits**: Implement reasonable limits on variable count and memory usage

## Implementation Phases

### Phase 1: Basic Variables and Expressions
1. [X] Implement variable storage system
2. [X] Create basic tokenizer for expressions
3. [X] Add support for assignment operator (`=`)
4. [X] Implement arithmetic operations (`+`, `-`, `*`, `/`)
5. [X] Add parentheses support for expression grouping

### Phase 2: Comparisons and Arrays
1. [X] Implement comparison operators (`<`, `<=`, `>`, `>=`, `==`)
2. [X] Add array variable support with indexing
3. [X] Extend tokenizer to handle array syntax
4. [X] Test variable resolution in existing shell commands

### Phase 3: Control Flow
1. [X] Implement command block parsing (`{`, `}`)
2. [X] Add if statement support with condition evaluation
3. [X] Create for loop implementation
4. [X] Add nested block support with proper variable scoping

### Phase 4: Integration and Polish
1. [X] Integrate scripting with existing shell functionality
2. [ ] Add comprehensive error handling and reporting
3. [X] Implement memory cleanup and resource management
4. [ ] Performance optimization and testing

### Phase 5: Program Return Value Capture
1. [X] Add U32 ExitCode field to TASK and PROCESS structures
2. [X] TaskRunner calls SYSCALL_Exit with exit code
3. [X] Modify Spawn() to return actual exit code instead of BOOL
4. [X] Add spawn() function support in scripting syntax
5. [X] Update ShellScriptExecuteCommand to handle return values
6. [X ] Test program return value capture in scripts

### Phase 6: Robust signaling mechanism (GetObjectExitCode can miss the code if data is deleted)

1. [X] Add a module to manage a dynamic array in which each element has a TTL (time to live)
2. [X] Use the module to store the finished state and exit code of each object (task, process, ...) with a 1 min TTL
3. [X] Put the TTL signal array in a dedicated field (ObjectTerminationState) in KernelData and use the kernel monitor to update TTL arrays
4. [X] Processes and tasks must store their exit code in Kernel.ObjectTerminationState
5. [X] Use the Kernel.ObjectTerminationState to get finished state and object exit codes in IsObjectSignaled()

## Syntax Examples

### Variables
```bash
name = "EXOS";
count = 42;
arr[0] = 10;
arr[1] = 20;
```

### Expressions
```bash
result = (a + b) * c;
valid = (x > 0) && (x < 100);
```

### Control Flow
```bash
if (count > 0) {
    print "Count is positive";
}

for (i = 0; i < 10; i = i + 1) {
    print i;
}
```

### Program Execution with Return Values
```bash
// Execute program and capture return value
result = spawn("/system/APPS/HELLO");
if (result == 0) {
    success = 1;
}

// Use return value in expressions
total = spawn("/system/APPS/CALC") + 10;
```

## Technical Considerations

### Memory Management
- Use existing EXOS heap allocation functions
- Implement reference counting for string variables
- Ensure proper cleanup on shell exit or error conditions

### Performance
- Minimize parsing overhead for non-scripting commands
- Cache frequently used variables
- Optimize expression evaluation for simple cases

### Error Handling
- Provide clear error messages with line/column information
- Graceful degradation for syntax errors
- Maintain shell stability during script execution

### Compatibility
- Preserve existing shell command functionality
- Ensure scripting features don't interfere with normal operation
- Maintain backward compatibility with current shell usage

### Program Return Value Implementation
- **Process Structure**: Add U32 ExitCode field to PROCESS structure
- **Runtime Integration**: Modify runtime/__start__ to capture eax from exosmain() return
- **Main Task Detection**: If terminating task is main task, set process exit code
- **Spawn Function**: Modify Spawn() to return I32 exit code instead of BOOL success
- **Script Integration**: Add spawn() as built-in function in expression parser
- **Command Execution**: Update ShellScriptExecuteCommand to handle spawn() calls

## Testing Strategy
1. Unit tests for individual components (tokenizer, parser, evaluator)
2. Integration tests for scripting features within shell
3. Stress tests for memory usage and resource limits
4. Compatibility tests with existing shell functionality
