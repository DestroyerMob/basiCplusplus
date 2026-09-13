# BasicC

BasicC is a small compiled language with indentation-based blocks, C-style
expressions, and shared comparisons. The current MVP runs programs
with integers, floats, booleans, strings, structs, arrays, explicit dynamic values,
and console input/output.
See [LANGUAGE.md](LANGUAGE.md) for the language contract
and [GOALS.md](GOALS.md) for the roadmap.

## Run a program

From the repository folder on macOS/Linux:

```sh
./bc examples/first_program.bc
```

Or run your own file with `./bc my_program.bc`. The launcher builds the compiler
automatically on first use and whenever its C++ sources or headers change,
then compiles and runs your program. It preserves interactive input and the
program's exit code, and removes its temporary executable afterward.

```sh
./bc examples/greeting.bc
./bc examples/first_program.bc --backend llvm -O2
./bc --check my_program.bc
./bc build my_program.bc -o build/my_program
```

Use `build` when you want to keep an executable. Native link options work too:
`./bc examples/interop/main.bc --link examples/interop/native.c`.
Paths remain relative to your current working directory. `./bc --help` lists
the commands. A C++17 compiler and a C compiler are required; the defaults are
`c++` and `cc`, configurable through `CXX` and `CC`.

## Build the compiler manually

For VS Code syntax highlighting, indentation, and snippets, install the
[BasicC editor extension](editors/vscode/README.md). It recognizes `.bc` files.

You need a C++17 compiler to build BasicC and Clang or GCC to compile the C it
generates. No external libraries are required. On macOS, the Xcode command-line
tools provide the compilers; on Linux, use your system's compiler packages.

From the repository directory:

```sh
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Wpedantic \
    main.cpp lexer.cpp parser.cpp ast_printer.cpp compiler.cpp modules.cpp \
    -o build/basicc
```

Alternatively, if CMake is installed:

```sh
cmake -S . -B build
cmake --build build
```

## Build and run your first program

```sh
./bc examples/first_program.bc
```

Expected output:

```text
4
true
false
55
true
```

Edit [examples/first_program.bc](examples/first_program.bc), rebuild, and run it
again to try different behavior. The example includes a helper function,
inferred variables and return types, a loop, branching, and shared comparisons.

Two more examples are available:

```sh
build/basicc build examples/numbers.bc -o build/numbers
./build/numbers
build/basicc build examples/greeting.bc -o build/greeting
./build/greeting
```

[numbers.bc](examples/numbers.bc) demonstrates floats and explicit conversions.
[greeting.bc](examples/greeting.bc) reads your name and a number, uses strings,
and compares string values with a shared comparison.

For structs, typed arrays, and member/index updates:

```sh
build/basicc build examples/collections.bc -o build/collections
./build/collections
```

For a program split across files, with shared types and functions:

```sh
build/basicc build examples/modules/main.bc -o build/modules_demo
./build/modules_demo
```

This prints `42`. Imports use quoted paths such as `import "math.bc";`, resolved
relative to the file containing the import. Shared dependencies load once.

For calls into C code:

```sh
build/basicc build examples/interop/main.bc \
    --link examples/interop/native.c -o build/interop
./build/interop
```

This prints `42`, `2.5`, and `7`. The example combines two functions in a C source
file with libc's `abs`, using the `c_int` annotation for its native C signature.

For pointers, borrowing, and a C function that updates a value:

```sh
build/basicc build examples/pointers.bc \
    --link examples/interop/native.c -o build/pointers
./build/pointers
```

Address-taking, dereferencing, and foreign calls use `unsafe:` blocks. The
compiler rejects local pointers escaping their lifetime and checks null
dereferences at runtime. Pointer copies and address comparisons are safe value
operations. See [LANGUAGE.md](LANGUAGE.md#pointers-and-unsafe-blocks) for the rules.

For namespaced C++ functions and overloaded signatures:

```sh
build/basicc build examples/cpp/main.bc --cpp-header examples/cpp/native.hpp \
    --link examples/cpp/native.cpp -o build/cpp_demo
./build/cpp_demo
```

This prints `10`, `2.5`, and `3`. Bindings use
`extern "C++" fn root(value: float) -> float = "demo::positive_root";`.
BasicC generates a C ABI wrapper and compiles it as C++17. Repeat `--cpp-header`
to include more declaration headers. `CXX` selects the C++ compiler and linker
(default `c++`), as one executable name or path. C++ exceptions at the boundary
print a diagnostic and terminate with exit code 1.

For explicit dynamic values, mixed collections, and checked casts:

```sh
build/basicc build examples/dynamic.bc -o build/dynamic
./build/dynamic
```

Use `any(value)` to box a value, `cast<Type>(value)` to extract its exact type,
and `type_name(value)` to inspect that type. Dynamic arithmetic checks payload
types and uses the same runtime guards as ordinary arithmetic. `var` alone
still infers a fixed type.

## Commands

| Command | Purpose |
| --- | --- |
| `basicc --tokens file.bc` | Print that file's lexical tokens. |
| `basicc --ast file.bc` | Print that file's syntax tree, including import declarations. |
| `basicc --check file.bc` | Check a complete executable program without invoking a C compiler. |
| `basicc --emit-c file.bc` | Check the program and write generated C to standard output. |
| `basicc --emit-cpp file.bc [--cpp-header file]` | Write generated C++ wrappers to standard output. |
| `basicc --emit-ir file.bc [-O0..-O3]` | Write LLVM IR to standard output using Clang. |
| `basicc build file.bc [-o output]` | Check, compile, and link an executable. |

Builds also accept repeated `--link file` options for C/C++ sources, objects, or
libraries, and `-l name` options for named linker libraries. All CLI file paths
are relative to the working directory. Extern signatures are checked by
`--check`, but their implementations are resolved only when linking.

## Optimization and LLVM

Builds default to the C backend with optimization disabled (`-O0`). Both
backends accept `-O0`, `-O1`, `-O2`, or `-O3`. Select the LLVM backend to generate
LLVM IR and then compile that IR into a native object before linking:

```sh
build/basicc build examples/collections.bc --backend llvm -O2 -o build/collections
./build/collections
build/basicc --emit-ir examples/first_program.bc -O2 > build/first_program.ll
```

The LLVM path reuses BasicC's checked C lowering. Clang converts that form to
LLVM IR and supplies optimization and native code generation; this is not a
separate handwritten machine-code generator. It supports the same language
features and C/C++ linking options as the C backend. Runtime guards remain active
at every optimization level.

`BASICC_CLANG` selects the Clang executable for IR and LLVM code generation
(default `clang`). `CC` selects the C compiler/linker driver (default `cc`).
Each setting is one executable name or path. LLVM IR is generated for the local
target and is best consumed with the same Clang version that produced it.

## Build output and toolchain

Without `-o`, the executable is written beside the source with its `.bc`
extension removed (`.exe` on Windows). Extensionless source files use `.out`
on macOS/Linux. The build command refuses to overwrite any input source,
including imports, explicit link files, and C++ headers, and keeps an existing executable if
compilation fails.
Build intermediates are temporary; use `--emit-c` to inspect the generated code.

The C backend invokes `cc` by default. Set `CC` to a Clang or GCC executable
name or path to select another compiler. `CC` is one executable, not a shell
command with flags. The MVP uses C11 and compiler overflow builtins.

```sh
CC=clang build/basicc build examples/first_program.bc -o build/first_program
```

Compiler commands return 0 on success, 1 for source/build errors, and 2 for
incorrect command usage. A compiled program returns its own exit status.

## What can execute

- Signed 64-bit integers, binary64 floats, booleans, and immutable strings, with
  strict type checking and explicit conversions.
- Global and local variables; lexical scopes and inner-scope shadowing.
- Functions, forward calls, inferred return types, and explicitly typed recursion.
- Arithmetic, comparisons, assignments, logical and bitwise operators, and
  conditional expressions.
- `if`/`else`, `while`, C-style `for`, and `return`.
- `a and b == 2` (all values match) and `a or b == 2` (at least one matches).
- Struct constructors, member access, arrays, and checked indexing.
- Imports with dependency ordering and diagnostics from the original source file.
- `extern fn` declarations for scalar C functions, with explicit ABI types.
- Generated C++ bridges for free functions and static methods, including overloads.
- Typed pointers, `unsafe` blocks, and conservative lifetime checking.
- Explicit `any` values, runtime type names, dynamic operations, and checked casts.
- `print(value)`, `input()`, and `len(value)` for basic console programs.

Every executable needs `fn main() -> int` with no parameters. An inferred
`int` return type is also accepted. Helper functions can return `int`, `float`,
`bool`, `string`, `any`, a struct, an array, a pointer, or `void`; a function without a value return
infers `void`.

Aggregate foreign calls remain a later goal. Structs copy their
fields; arrays share mutable storage. Arrays have fixed storage lengths, and
recursive struct types are not supported yet. Allocated string and array storage
currently lives until process exit. The initial pointer checker excludes pointer
fields, pointer arrays, pointer-to-pointer types, and pointer arithmetic.
