# BasicC Goals

## Main goal

Make it possible to write a BasicC source file, compile it locally, and run the
resulting executable. Start with a small, useful language subset, then expand
toward the systems-language goals in [LANGUAGE.md](LANGUAGE.md).

## Current capabilities

BasicC can tokenize and parse `.bc` files, check the executable
subset, generate C, and compile/link runnable programs locally. The command line
supports `--tokens`, `--ast`, `--check`, `--emit-c`, `--emit-cpp`, `--emit-ir`,
and `build`. It reports source errors with line and column information.
See [README.md](README.md) for
build instructions and a runnable example.

Implemented syntax includes:

- Functions, typed parameters, and optional return-type annotations.
- Global and local variables with explicit types or inferred-type syntax.
- Arithmetic, comparisons, assignments, and logical and bitwise operators.
- Function calls, indexing, and member access.
- `if`/`else`, `while`, `for`, and `return`.
- Shared comparisons such as `a and b == 2` and `a or b == 2`.

Name resolution, type inference, type checking, and executable generation now
cover integers, floats, booleans, strings, structs, and arrays. Console input/output,
explicit conversions, checked indexing, member reads/updates, and imports work.
Programs can share declarations across files with dependency ordering and
diagnostics that retain the originating file. C functions with scalar and typed
scalar-pointer signatures can be declared with `extern fn` and linked from C
sources, objects, and libraries. Unsafe blocks and conservative pointer lifetime
checks cover raw operations and escaping local borrows. An LLVM backend exposes
IR, supports optimization levels, and compiles native objects before linking.
C++ bindings generate C ABI wrappers with exact overload selection and exception
handling, compiled and linked automatically from explicit headers and sources.
Explicit `any` values carry runtime type tags, support checked operations and
casts, and can hold scalars, structs, or arrays without hiding borrowed pointers.

## Milestone 1: Check program meaning

- [x] Track global, function, parameter, and local scopes.
- [x] Resolve variable references and function calls; diagnose undefined names
  and invalid duplicate declarations.
- [x] Define integer and boolean semantics, including integer width, overflow,
  conversions, and valid operations.
- [x] Infer variable types from initializers and function return types where
  annotations are omitted.
- [x] Check assignments, call argument counts and types, conditions, and returns.
- [x] Require grouped operands in shared comparisons to have the same type and
  require the shared target to support the selected comparison.
- [x] Define evaluation order and whether shared comparisons short-circuit,
  including how often operand and target expressions are evaluated.
- [x] Report semantic errors with source locations and clearly reject features
  outside the first executable subset.

Completion means supported programs have resolved names and checked types before
code generation begins. Invalid programs receive useful diagnostics.

## Milestone 2: Generate executable code

- [x] Use C generation and an existing local Clang/GCC compiler for the initial
  code-generation route; a dedicated native backend remains a later option.
- [x] Translate checked integer and boolean expressions, variables, functions,
  calls, branches, loops, and returns.
- [x] Translate shared comparisons while preserving their defined evaluation
  behavior and keeping `&&` and `||` as condition-combining operators.
- [x] Compile and link generated output into a local executable.

Completion means the supported subset produces executable code with behavior
consistent with the language contract.

## Milestone 3: Define program startup and basic output

- [x] Define the entry-point contract for `main`.
- [x] Define process exit codes and how `main` returns them.
- [x] Provide minimal output for integers and booleans so programs can display
  results before full string support exists.
- [x] Supply any runtime support needed by the initial executable subset.

Completion means a program can start, perform a calculation, display its result,
and exit successfully.

## Milestone 4: Make building a program straightforward

- [x] Add `basicc build <file.bc> [-o <executable>]`.
- [x] Connect lexing, parsing, semantic checking, code generation, compilation,
  and linking in that command.
- [x] Define executable output naming and an option to choose the output path.
- [x] Report missing local compiler dependencies and compilation/linking failures
  clearly, with appropriate command exit codes.
- [x] Document local build prerequisites and the steps to build and run a BasicC
  program.

Completion means a user can build a source file and run its executable without
manually coordinating the compiler stages.

## First runnable release

The implemented MVP supports single-file programs using integers, booleans,
variables, functions, branches, loops, shared comparisons, and basic output.
It makes small calculation tools and command-line programs possible.
[examples/first_program.bc](examples/first_program.bc) demonstrates the subset.

The release is ready when a user can:

1. Write a program with a `main` function and a helper function.
2. Use variables, arithmetic, control flow, and shared comparisons.
3. Build it locally with one BasicC command.
4. Run it, see the expected output, and receive the expected exit code.
5. Receive useful diagnostics for syntax errors, undefined names, and type
   mismatches.

This is the initial completion target; implementing every parsed construct is
not required for it. Features outside this subset must be diagnosed explicitly.

## Later goals

- [x] Floating-point execution and defined numeric conversions.
- [x] Strings, broader input/output, and a growing standard library.
  The initial library provides `print`, `input`, `len`, and explicit scalar
  conversions for console programs.
- [x] Structs, collections, indexing, and member semantics.
  Typed fields, positional constructors, fixed-length typed arrays, checked
  access, and content equality are implemented; recursive struct types and
  resizing are not part of the initial collection implementation.
- [x] Imports and programs split across multiple source files.
- [x] `extern` declarations and C ABI interoperability.
  The initial boundary supports scalar signatures, native C `int` range checks,
  and explicit linking, plus typed scalar pointers inside unsafe blocks.
- [x] Pointers, `unsafe` contexts, and semantic/lifetime checks for memory safety.
  Typed address/dereference operations, null checks, and a conservative lifetime
  flow graph are implemented. Pointer fields/arrays, pointer-to-pointer types,
  arithmetic, and broader ownership management remain outside this initial model.
- [x] A richer intermediate representation, optimizations, and a dedicated
  native backend if the initial route emits C.
  The LLVM backend exposes IR and compiles it to native objects, with `-O0`
  through `-O3`. It reuses checked C lowering through Clang/LLVM rather than
  implementing a handwritten instruction selector.
- [x] Generated C++ bridges after C interoperability.
  Explicit C++ targets, exact overload selection, C ABI wrappers, exception
  containment, and C++17 compilation/linking work with both backends.
- [x] Explicit dynamic typing through the `any` type.
  Explicit boxing, checked casts, dynamic arithmetic/comparisons, type names,
  formatting, and mixed collections work with both backends. Borrowed pointers
  cannot be hidden in boxes; recursive dynamic values have runtime depth guards.

These goals extend the first runnable release toward C-like performance and
interoperability, safer defaults, and convenient syntax. They are not
prerequisites for the initial integer-and-boolean programs.
