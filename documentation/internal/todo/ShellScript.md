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
1. Implement variable storage system
2. Create basic tokenizer for expressions
3. Add support for assignment operator (`=`)
4. Implement arithmetic operations (`+`, `-`, `*`, `/`)
5. Add parentheses support for expression grouping

### Phase 2: Comparisons and Arrays
1. Implement comparison operators (`<`, `<=`, `>`, `>=`, `==`)
2. Add array variable support with indexing
3. Extend tokenizer to handle array syntax
4. Test variable resolution in existing shell commands

### Phase 3: Control Flow
1. Implement command block parsing (`{`, `}`)
2. Add if statement support with condition evaluation
3. Create for loop implementation
4. Add nested block support

### Phase 4: Integration and Polish
1. Integrate scripting with existing shell functionality
2. Add comprehensive error handling and reporting
3. Implement memory cleanup and resource management
4. Performance optimization and testing

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

## Testing Strategy
1. Unit tests for individual components (tokenizer, parser, evaluator)
2. Integration tests for scripting features within shell
3. Stress tests for memory usage and resource limits
4. Compatibility tests with existing shell functionality
