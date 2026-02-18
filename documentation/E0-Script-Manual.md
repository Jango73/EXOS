# E0 Script Language — Developer Manual

> Version: Phase 1 — Core syntax, variables, control flow  
> For EXOS Shell — Lightweight Embedded Command Language

E0 is the internal **shell scripting language** of the EXOS kernel.
It is designed for automation in EXOS shell.
Its syntax is intentionally minimal — inspired by C and early JS — and can be embedded or extended through host callbacks.

---

## 1) Lexical elements

### Identifiers
- Letters, digits, and `_`, starting with a letter or `_`.
- Case-sensitive.

### Literals
- **Number**: floating-point; integers are a subset (e.g., `0`, `42`, `3.14`).
- **String**: single `'...'` or double quotes `"..."` with escapes: `\n`, `\r`, `\t`, `\\`, `\'`, `\"`.
- **Path**: a token starting with `/` and not immediately followed by whitespace or `//`. Treated as a shell command token.

### Tokens / Operators
- Arithmetic: `+`, `-`, `*`, `/`
- Comparisons: `<`, `<=`, `>`, `>=`, `==`, `!=`
- Indexing: `[` `]`
- Grouping: `(` `)`, block `{` `}`
- Assignment: `=`
- Member access: `.`
- Statement terminator: `;`

### Keywords
- `if`, `else`, `for`

> **Comments:** not implemented yet. A `/` at start of a line may be parsed as a shell command.

---

## 2) Types & values

Runtime types:
- **float** (default numeric)
- **int** (tracked when integer-valued)
- **string**
- **array** (sparse, auto-grown)
- **host-handle** (opaque reference provided by the host)

Numeric coercion:
- Arithmetic uses float unless both operands are int.
- `0` is **false**, nonzero is **true**.
- Comparisons yield `1.0` or `0.0`.

Strings are immutable. Arrays can contain mixed values.

String operators:
- `string + string`: concatenation
- `string - string`: removes all occurrences of the right string from the left string (`"foobarfoo" - "foo"` gives `"bar"`)

---

## 3) Variables and assignment

- `x = expression;`
- Variables auto-declare in current scope.
- Lookups search upward through scopes.
- Host-registered symbols cannot be reassigned.

Semicolons:
- Required after assignments.
- Optional after blocks, control flow, and expression statements.

```text
x = 1 + 2*3;
y = (x > 5);
msg = "hi\nthere";
```

Blocks `{ ... }` create a new scope. Assigning an existing name updates the parent.

---

## 4) Arrays

- Arrays are created on first indexed assignment.
- Index must be numeric.
- Reading an unset element triggers an error.

```text
a[0] = "zero";
a[1] = 10;
i = 2;
a[i] = a[1] * 3;
val = a[0];
```

---

## 5) Expressions

Operator precedence (high → low):
1. Parentheses
2. Index/property: `expr[index]`, `expr.property`
3. Unary `+`, `-`
4. `*`, `/`
5. `+`, `-`
6. Comparisons

Left-associative.

```text
x = 1 + 2 * 3;
ok = (x >= 7) == 1;
z = (10 / 4);
name = "foo" + "bar";
trimmed = "foobarfoo" - "foo";
```

---

## 6) Host interop: functions, properties, and commands

### Function calls
Syntax: `name(expr?)`  
- Only one optional argument.
- Argument is stringified before calling the host.

```text
echo("hello");
ping(123);
```

### Property access
`base.property` resolves through the host’s descriptor.

```text
user.name;
disk.size;
```

### Shell command statements
At statement start:
- Quoted string → command  
- Path starting with `/` → command  
- Bare identifier without `=` or `(` → command

```text
"/usr/bin/echo hello world"
/usr/bin/echo hello
echo hello
```

---

## 7) Control flow

### If / Else
```text
if (x > 0) {
    pos = 1;
} else {
    pos = 0;
}
```

### For loop
```text
for (i = 0; i < 10; i = i + 1) {
    sum = sum + i;
}
```
Capped at 1000 iterations.

---

## 8) Errors

- `SYNTAX_ERROR`
- `UNMATCHED_BRACE`
- `OUT_OF_MEMORY`
- `TYPE_MISMATCH`
- `DIVISION_BY_ZERO`
- `UNDEFINED_VAR`

API:
```c
ScriptGetLastError(ctx);
ScriptGetErrorMessage(ctx);
```

---

## 9) Embedding API (C/C++)

### Context
```c
LPSCRIPT_CONTEXT ScriptCreateContext(const SCRIPT_CALLBACKS* cb);
void ScriptDestroyContext(LPSCRIPT_CONTEXT ctx);
```

### Callbacks
```c
typedef struct SCRIPT_CALLBACKS {
    U32 (*ExecuteCommand)(LPCSTR cmdline, void* user);
    U32 (*CallFunction)(LPCSTR name, LPCSTR arg, void* user);
    void* UserData;
} SCRIPT_CALLBACKS;
```

### Variables
```c
ScriptSetVariable(...);
ScriptGetVariable(...);
ScriptPushScope(...);
ScriptPopScope(...);
```

### Arrays
```c
ScriptSetArrayElement(...);
ScriptGetArrayElement(...);
```

### Host symbols
```c
ScriptRegisterHostSymbol(...);
ScriptUnregisterHostSymbol(...);
```

---

## 10) Example embedding

```c
static U32 Exec(const char* cmd, void* u) {
    return 0;
}
static U32 Call(const char* name, const char* arg, void* u) {
    return 0;
}

void run(const char* code) {
    SCRIPT_CALLBACKS cb = { Exec, Call, NULL };
    LPSCRIPT_CONTEXT ctx = ScriptCreateContext(&cb);
    if (ScriptExecute(ctx, code))
        printf("%s\n", ScriptGetErrorMessage(ctx));
    ScriptDestroyContext(ctx);
}
```

---

## 11) Grammar (simplified)

```
program    := { statement [ ';' ] }
statement  := assignment ';' | ifStmt | forStmt | block | exprStmt
block      := '{' { statement [ ';' ] } '}'

assignment := IDENT ( '[' expr ']' )? '=' expr
ifStmt     := 'if' '(' expr ')' statement [ 'else' statement ]
forStmt    := 'for' '(' assignment ';' expr ';' assignment ')' statement
exprStmt   := expression
```

---

## 12) File extension

- Scripts use the `.e0` extension.  
  Example: `startup.e0`, `mount.e0`, `nettest.e0`

---

## 13) Example scripts

```text
// System setup (no comment support yet, pseudo-comment)
echo("Booting EXOS...");
/bin/init --fast

if (mem.free < 100) {
    echo("Warning: low memory");
}

for (i = 0; i < 5; i = i + 1) {
    echo("Tick " + i);
}
```

---

## 14) Philosophy

E0 is not a general-purpose language.  
It’s a **shell-oriented control language** — small, embeddable, deterministic — meant for task automation and quick scripting inside EXOS subsystems.
