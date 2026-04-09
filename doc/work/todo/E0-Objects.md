# E0 Objects

## Goal

Extend the E0 scripting engine so scripts can create and manipulate dynamic objects in a JavaScript-like way.

Target behavior:

- an object can hold any number of named properties
- a property can store any existing E0 value type
- a property can also store another object
- properties are created on first assignment
- there is no declared schema and no static typing

Example target syntax:

```text
user = {};
user.name = "alice";
user.age = 32;
user.settings = {};
user.settings.theme = "light";
```

Out of scope for this step:

- methods or callable object members
- prototype/inheritance logic
- object iteration helpers
- object equality rules beyond existing scalar behavior
- JSON parser or serializer

## Implementation Steps

### Step 1. Add a native script object value type

Goal:

- represent one dynamic object entirely inside the E0 runtime, without relying on host descriptors

Required work:

- add `SCRIPT_VAR_OBJECT` to the runtime value type enum in [Script.h](/home/jango/code/exos/kernel/include/script/Script.h)
- define one reusable `SCRIPT_OBJECT` container with:
  - owning script context
  - property count
  - property storage for `name -> SCRIPT_VALUE`
- place object allocation, lookup, set, release, and destroy helpers next to the other runtime collection helpers in [Script-Collections.c](/home/jango/code/exos/kernel/source/script/Script-Collections.c)
- extend `ScriptValueRelease()` and all value-copy/ownership paths so nested objects are released correctly

Result:

- E0 gains a first-class runtime object container, managed like arrays and strings

### Step 2. Extend the parser for object creation and property assignment

Goal:

- let scripts create empty objects and assign properties with dotted paths

Required work:

- add minimal object literal parsing in [Script-Parser-Expression.c](/home/jango/code/exos/kernel/source/script/Script-Parser-Expression.c):
  - only `{}` for the first iteration
  - do not parse inline object members yet
- extend assignment parsing so the left side accepts:
  - `name = expr;`
  - `name[index] = expr;`
  - `name.property = expr;`
  - `name.property.subproperty = expr;`
- keep host function calls and host property reads compatible with the existing grammar
- update AST structures in [Script.h](/home/jango/code/exos/kernel/include/script/Script.h) only as much as needed to encode property-assignment targets cleanly

Result:

- the parser can distinguish property writes from normal variable assignment

### Step 3. Evaluate object reads and writes in the runtime

Goal:

- make object property access work for both reading and writing

Required work:

- extend expression evaluation in [Script-Eval.c](/home/jango/code/exos/kernel/source/script/Script-Eval.c) so `base.property` supports two runtime branches:
  - host-backed objects through the existing descriptor path
  - script-native objects through the new object container
- extend assignment execution so property assignment:
  - resolves the base object chain
  - rejects non-object intermediate values
  - creates the final property on first assignment
  - replaces an existing property value safely
- keep undefined property reads explicit and deterministic:
  - first version should return an E0 error rather than inventing an implicit `undefined` value

Result:

- E0 supports `obj.name` reads and writes with nested objects

### Step 4. Lock the feature behind focused autotests

Goal:

- validate the new runtime semantics before using objects in shell scripts

Required work:

- add object coverage to [Autotest-Script.c](/home/jango/code/exos/kernel/source/autotest/Autotest-Script.c)
- cover at least:
  - empty object creation
  - first property assignment
  - property overwrite
  - nested object creation
  - mixed types inside one object
  - read from missing property -> explicit error
  - invalid write through non-object intermediate -> explicit error
  - object lifetime cleanup after context destruction

Result:

- the feature is protected against parser, evaluator, and ownership regressions

### Step 5. Document the new E0 object model

Goal:

- keep E0 language documentation aligned with the runtime behavior

Required work:

- update [E0-Scripting.md](/home/jango/code/exos/doc/guides/E0-Scripting.md):
  - add `object` to the runtime types section
  - document `{}` creation
  - document `obj.property` read/write semantics
  - document missing-property errors
- update [Kernel.md](/home/jango/code/exos/doc/guides/Kernel.md) if it references E0 capabilities at the engine level

Result:

- the object model is specified and usable without reading implementation details

## Suggested Order

1. Runtime object container
2. Parser support for `{}` and dotted assignment targets
3. Evaluator support for object reads and writes
4. Autotests
5. Documentation

## Validation

- `git diff --check`
- `bash scripts/linux/build/build --arch x86-32 --fs ext2 --debug --clean`
- `bash scripts/linux/build/build --arch x86-64 --fs ext2 --debug --clean`
- run the script autotest suite and add new object-focused cases before using the feature in shell embedded scripts
