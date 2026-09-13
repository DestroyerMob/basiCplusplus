# BasicC Language Contract (v0.11 MVP)

BasicC is intended to be a statically compiled systems language with C-like
performance and interoperability, safer defaults, and scripting-language
ergonomics. This document covers the parsed syntax and the initial executable
subset. Some syntax is accepted by `--ast` but awaits execution support.

## Source files and layout

- Source files use UTF-8 and normally end in `.bc`.
- LF and CRLF line endings are accepted. A UTF-8 byte-order mark is optional.
- Identifiers currently contain ASCII letters, digits, and underscores and
  cannot begin with a digit.
- Blocks begin after `:` and use four spaces per indentation level. Tabs are
  invalid, and a dedent must return to an earlier indentation level.
- Ordinary statements end with `;`. A block header ending in `:` does not.
- Empty lines and comment-only lines do not change indentation.

```basicc
fn maximum(left: float, right: float) -> float:
    if left > right:
        return left;
    else:
        return right;
```

## Initial lexical elements

Several declarations and built-in types have equivalent spellings. Each group
produces the same token and will have identical meaning in later compiler
stages; tools preserve the spelling originally written by the programmer.

| Canonical spelling | Accepted aliases |
| --- | --- |
| `fn` | `func`, `function` |
| `var` | `variable` |
| `int` | `integer` |
| `float` | `double` |
| `bool` | `boolean` |
| `string` | `str` |

The other reserved words are `if`, `else`, `while`, `for`, `return`, `import`,
`extern`, `unsafe`, `struct`, `true`, `false`, `null`, `and`, and `or`. Fixed-width type names such
as `i32` remain ordinary identifiers until
their semantics are defined.

Decimal integer literals contain digits. Floating-point literals use one
64-bit `float`/`double` type and may contain a fractional part or exponent, such as
`3.5`, `1e6`, or `2.5e-3`. A leading minus is a separate token.

Strings use double quotes and cannot span lines. The recognized escapes are
`\"`, `\\`, `\n`, `\r`, `\t`, and `\0`. The lexer validates escapes but keeps
the original source text for later compiler stages.

BasicC supports `//` line comments and non-nested `/* ... */` block comments.

The initial operators and punctuation are:

```text
+  -  *  /  %  =  ==  !  !=  <  <=  >  >=
&  &&  |  ||  ?  ->  (  )  [  ]  .  ,  :  ;
```

## Type-system direction

Declarations are statically typed. `var` asks the compiler to infer the type;
it does not create a dynamically typed value. `any` makes runtime typing
explicit, with boxing and checked casts. Raw pointer operations require an
`unsafe` context. `any`, `cast`, and `type_name` are reserved built-in names,
recognized from identifier tokens.

## Implemented syntax

Functions use typed parameters. Their return type may be explicit or inferred:

```basicc
fn explicit(value: int) -> bool:
    return value > 0;

function inferred(value: integer):
    return value * 2;
```

Variables must have initializers and may use inferred, annotated, or type-first
declarations:

```basicc
var inferred = 10;
variable annotated: integer = 20;
int type_first = 30;
```

Module-level variables and functions are supported. Function blocks may contain
variable declarations, expression statements, `return`, `if`/`else if`/`else`,
`while`, and C-style `for` statements. C-style loops require parentheses, and
each clause may be empty:

```basicc
for (var index = 0; index < 10; index = index + 1):
    process(index);

for (;;):
    poll();
```

Expressions follow C-style precedence and support assignment, ternary
conditionals, logical and bitwise operators, comparisons, arithmetic, unary
operators, calls, indexing, and member access. Assignments may target only an
identifier, member, or indexed expression.

`unsafe:` introduces a block inside a function where raw memory operations and
foreign calls are permitted. It does not disable type, bounds, or lifetime checks.

## Shared comparisons

`and` and `or` group two or more values of the same type for comparison with
one shared state. `and` requires every value to satisfy the comparison; `or`
requires at least one. The result is a boolean. These keywords do not join
independent conditions; `&&` and `||` serve that purpose.

```basicc
fn matching(a: int, b: int, c: int) -> bool:
    var both = a and b == 2;
    var either = a or b == 2;
    var all_below = a and b and c < 10;
    return both && either;
```

The supported shared operators are `==`, `!=`, `<`, `<=`, `>`, and `>=`.
For example, `a or b != 2` means at least one value differs from 2.
Each group must use only `and` or only `or`; mixing them in one group is an
error. Combine complete comparisons with `&&` or `||` instead.

Arithmetic, unary operators, calls, indexing, and member access bind within
each operand and the target: `a + 1 and items[0] == limit - 1` compares two
values against `limit - 1`. A complete shared comparison binds more tightly
than `&`, `|`, `&&`, `||`, ternary conditionals, and assignment. Parentheses
allow more complex operand or target expressions.

A group requires one comparison operator and one target. Bare `a and b`,
`a == 2 and b == 2`, and `a and b == 2 == true` are syntax errors. To compare
the resulting boolean, write `(a and b == 2) == true` explicitly.

The syntax tree preserves the operand group and a single target expression.
All grouped values must have the same type, and the target must have that same
type. Type aliases count as the same type. The executable subset enforces these
requirements. Equality and inequality accept integers, floats, booleans, strings,
structs, arrays, and typed pointers; ordering comparisons require integers or floats. Strings
compare their contents, including embedded NUL bytes. Structs compare their
fields and arrays compare their lengths and elements recursively. Pointers
compare addresses without dereferencing.

Grouped operand expressions are evaluated once each, left to right, followed
by the target expression once. All are evaluated even if an earlier comparison
would decide the result. This describes evaluation order, not parallel
execution. `&&` and `||` retain their usual short-circuit behavior.

## Executable subset

`--check`, `--emit-c`, `--emit-ir`, and `build` perform name resolution and type checking.
The executable subset supports scalar types, structs, arrays, variables,
functions, branches, loops, expressions, and shared comparisons. Other parsed features
receive an unsupported-feature diagnostic during these commands.

- `int` is a signed 64-bit integer. Arithmetic overflow, division by zero, and
  remainder by zero terminate execution with a runtime diagnostic and status 1.
  Integer division truncates toward zero. `INT64_MIN % -1` is zero.
- `bool` has the values `true` and `false`. There are no implicit conversions
  between scalar types. Conditions and `!`, `&&`, `||` require booleans;
  bitwise `&`, `|`, and remainder `%` require integers. Arithmetic `+`, `-`, `*`,
  and `/` accepts matching integer or float operands; `+` also joins strings.
- `float` and `double` are aliases for one binary64 type. Literals must fit the
  finite range. Operations that produce non-finite results and division by zero
  terminate with a runtime diagnostic and status 1. Underflow may round to a
  subnormal value or zero. Equality is exact; it does not use a tolerance.
- Every variable has an initializer. An annotation must match its initializer;
  otherwise its type is inferred. Names resolve through enclosing lexical
  scopes. Duplicate names in the same scope are errors; inner scopes may shadow
  outer variables. The initializer is resolved before the new variable enters
  scope, allowing `var x = x + 1` to refer to an outer `x`.
- Global variables initialize in declaration order before `main`. Initializers
  may read earlier globals but cannot call functions or assign other variables
  in this release. Built-in conversions, boxing/casts, and struct constructors are allowed.
- Function parameters have explicit types. Calls require matching argument
  counts and types. Forward calls work. Return types can be inferred, but a
  recursive inference cycle requires an explicit return annotation.
- `void` is accepted as a function return annotation. Helpers without value
  returns infer `void`; `return;` is valid in them. Void calls are statements,
  not values. Value-returning functions must return on every path; literal
  `while true` and conditionless `for` loops also prevent fallthrough.
- Expressions and call arguments evaluate left to right. Assignment resolves
  its destination (including index evaluation and bounds checks), then evaluates
  its value and stores it. `&&`, `||`, and `?:` evaluate only the branch
  needed for their result. Shared comparisons use the eager order above.
- `print(value)` is a reserved built-in function accepting one scalar, struct,
  array, pointer, or `any`. It writes the value and a newline and returns `void`.
  Floats print with up to 17 significant digits; booleans print `true` or `false`.
- An executable requires a parameterless `main` returning `int`. Its result is
  mapped to a process exit status using the low eight bits (0–255).

For example:

```basicc
fn main() -> int:
    var a = 2;
    var b = 2;
    print(a and b == 2);
    return 0;
```

## Strings, input, and conversions

Strings are immutable byte sequences. Literals support the escapes documented
above. Assignment and function calls share immutable storage. `+` concatenates
two strings, and `==`/`!=` compare contents. `len(text)` or `text.length` returns
the number of bytes, including embedded NULs; this is not a Unicode character
count. `text[index]` returns a one-byte string with a bounds check. Strings and
their lengths cannot be modified through member/index assignment.

`input()` reads one line from standard input, removing its LF or CRLF line
ending. It returns an empty string at end of input. `print`, `input`, and `len`
are reserved built-in function names and cannot be redeclared.

Type names can be called for explicit conversion; their aliases work too:

| Expression | Meaning |
| --- | --- |
| `float(integer_value)` | Convert to binary64; large integers may round. |
| `int(float_value)` | Truncate toward zero; values outside `[-2^63, 2^63)` fail at runtime. |
| `int(text)` | Parse a signed decimal integer within the signed 64-bit range. |
| `float(text)` | Parse a finite decimal number, optionally with a fractional part or exponent. |
| `string(value)` | Format a scalar, struct, array, pointer, or `any` as text. |
| `int(int_value)`, `float(float_value)`, `string(text)` | Preserve a value already of that type. |

Numeric text may have a leading sign but must have no whitespace, embedded NUL,
or trailing characters. Invalid numeric text produces a runtime diagnostic.
Booleans do not convert to numbers. Mixing numeric types in expressions or
shared comparisons requires an explicit conversion.

```basicc
fn main() -> int:
    print("Enter a number:");
    var number = float(input());
    print("Half is " + string(number / 2.0));
    return 0;
```

The initial runtime retains allocated string, array, and boxed storage until process
exit, when it frees that storage. This keeps returned values and shared storage
valid without reference counting, but repeated allocation in a long-running loop
can grow memory usage. Finer-grained lifetime management remains a later goal.

## Structs and arrays

A module-level struct defines typed fields. Construct a value by calling the
type name with one argument per field in declaration order:

```basicc
struct Person:
    name: string;
    age: int;

fn main() -> int:
    var person = Person("Ada", 30);
    person.age = 31;
    print(person.name);
    print(person);
    return 0;
```

Structs are nominal types: different declarations remain different types even
if their fields have the same shape. Field names must be unique. Each field
requires a type; field defaults, methods, and recursive struct types are not
supported yet. Structs can contain other structs or arrays, and may refer to
types declared later in the file.

Struct assignment and argument passing copy the fields by value. Array fields
copy their descriptors and share element storage. A struct field is writable
when its containing struct is stored in a variable or array element; assigning
a field of a temporary returned struct is rejected.

Array literals contain values of one type. Append `[]` to an element type for
annotations, parameters, fields, and return types; repeated suffixes describe
nested arrays:

```basicc
var scores = [10, 20, 30];
int[] other = [40, 50];
var empty: int[] = [];
var rows: int[][] = [[], [1, 2]];
```

An empty literal requires type context from an annotation, argument, assignment,
return type, or comparison. A bare `var empty = [];` cannot infer an element
type. Array length is determined at creation and is not part of the type; nested
arrays may have different lengths.

`values[index]` reads or assigns an element. Indexes must be integers in
`[0, len(values))`; invalid reads or writes terminate with a runtime diagnostic
and status 1. `len(values)` and the read-only `values.length` return the element
count. Arrays can contain scalar values, structs, or other arrays.

Array assignment and argument passing share mutable element storage. Replacing
an array variable with a different array changes that variable's descriptor;
aliases keep referring to the previous storage. There are no resize operations
in this release.

`==` and `!=`, including shared comparisons, compare aggregate contents. All
operands must have the same type. Evaluating an array expression copies its
descriptor, not its storage: later side effects remain visible through aliases.
`print(value)` and `string(value)` format struct fields and array elements for
inspection; the display format is not a serialization format.

## Modules

Module-level imports use a quoted source path and a semicolon:

```basicc
import "math.bc";
import "models/person.bc";
```

Paths are resolved relative to the importing source file, not the shell's
working directory. Absolute paths also work. The loader canonicalizes paths,
including symlinks, and loads each file once. A symlinked module resolves its
imports from the directory containing its canonical target.

Dependencies are processed before the importing file, in import order. Each
file's non-import declarations keep their original order. This also defines
global initialization order: imported globals initialize before their importers'
globals. An import cycle is a source error.

All loaded files currently share one global namespace. Functions and structs
can refer to declarations in other loaded files; duplicate global names are
errors. There are no aliases, private exports, package searches, or runtime
imports in this release. Imports are allowed only at module scope and must
refer to regular source files.

`--check`, `--emit-c`, `--emit-ir`, and `build` load the dependency graph. `--tokens` and
`--ast` inspect only the requested file, so syntax can be inspected without
loading dependencies. Errors in imported code retain its filename, line, and
column. Missing files and cycles are reported at the import that requested them.

See [examples/modules/main.bc](examples/modules/main.bc) for a complete program.

## C interoperability

Declare a foreign C function at module scope with `extern fn`. It requires typed
parameters, an explicit return type, and a semicolon instead of a body:

```basicc
extern fn native_answer() -> int;
extern fn native_half(value: float) -> float;
extern fn abs(value: c_int) -> c_int;

fn main() -> int:
    unsafe:
        print(native_answer());
        print(native_half(5.0));
        print(abs(-7));
    return 0;
```

The BasicC name is the C symbol name. An extern declaration must match the
actual C function's signature. The initial ABI supports these types:

| BasicC annotation | C ABI type | Value seen by BasicC callers |
| --- | --- | --- |
| `int` / `integer` | `int64_t` | `int` |
| `float` / `double` | `double` | `float` |
| `bool` / `boolean` | `_Bool` (`bool` from `<stdbool.h>`) | `bool` |
| `c_int` | The platform's C `int` | `int` |
| `int*`, `float*`, `bool*` | `int64_t*`, `double*`, `_Bool*` | The corresponding typed pointer |
| `void` | `void`, for returns only | No value |

`c_int` is an ABI annotation for extern signatures, not another BasicC variable
type. Integer arguments are range-checked before narrowing to C `int`; return
values widen to BasicC `int`. External float results must be finite or the
runtime reports an error. Other behavior inside the foreign implementation
follows that implementation's contract, including its arithmetic and side effects.

All extern calls require an `unsafe` block. C code must honor the declared types
and validity of borrowed pointers and must not retain a borrowed pointer beyond
its valid lifetime. Lifetimes and ownership of pointers originating in C are
the caller's responsibility; the checker cannot inspect foreign implementations.

Strings, arrays, structs, pointers to aggregates, `c_int*`, `void*`, variadic
declarations, callbacks, and extern globals are not supported at the initial C
boundary. `main` and names
beginning with `bc_` are reserved for the generated runtime. Extern declarations
can be shared through imports but cannot be redeclared under the same name.

Use `--link` to supply C source, object, or library files. Use `-l` to select a
library by its linker name. Both options may be repeated:

```sh
build/basicc build examples/interop/main.bc \
    --link examples/interop/native.c -o build/interop
./build/interop
```

Link paths are relative to the shell's working directory. Explicit link files
keep their option order; named libraries follow them. C sources are compiled
alongside the generated C, while object/library files go to the linker.
`--check` checks declarations and calls without resolving external symbols;
`build` reports compiler/linker failures if an implementation is unavailable.

## Explicit dynamic values

`any(value)` boxes a scalar, struct, or typed array with its exact runtime type.
`any` variables may receive boxes of different types over time. Ordinary
inferred variables retain their static type; boxing and unboxing are explicit:

```basicc
var value: any = any(40);
value = value + any(2);
print(cast<int>(value));
value = any("ready");
print(type_name(value));
```

`cast<Type>(value)` requires an `any` source and checks for the exact destination
type at runtime. A mismatch terminates with a diagnostic and exit code 1.
Targets may be scalars, structs, typed arrays (including nested arrays), or
`any` itself. Struct identity is nominal; an `int[]` cannot be cast to `any[]`.
`any(existing_any)` and `cast<any>(existing_any)` preserve the existing box.
There is no empty/uninitialized box or dynamic `null` payload.

`any` works in parameters, returns, globals, struct fields, and array elements.
For a mixed collection, use `any[]`, for example `[any(1), any("hello")]`.
Boxing copies a value into runtime storage; copying an `any` shares that
immutable box. Struct fields retain value semantics and arrays retain shared
mutable storage. Cast an array or struct to its concrete type before indexing
or accessing members. Casting an array preserves its storage sharing; casting
a struct copies its fields. Boxes and their owned allocations live until
process exit, like the existing string/array arena.

Two `any` values support `+`, `-`, `*`, `/`, `%`, `&`, and `|` when their payloads
have the same type and that type supports the operator. Results are boxed;
integer overflow, zero division, and finite-float checks still apply. String
payloads support `+`. Unary `+` and `-` require a numeric payload. There are no
implicit numeric promotions, implicit boxing, or truthiness conversions.
Conditions, `!`, `&&`, and `||` require an ordinary `bool`; use `cast<bool>` to
extract a boxed boolean.

Equality returns an ordinary `bool`: differing payload types compare unequal;
equal types use their normal scalar/content equality. Ordering requires two
payloads of the same numeric type. Shared comparisons require **every operand
and the target** to have the same payload type, including for `==` and `!=`.
All operand expressions and the target run once in the usual order, then all
type tags are checked before combining comparisons. An early match cannot hide
a later type mismatch.

`type_name(value)` returns a string describing the static type of an ordinary
value or the payload type of an `any`, using canonical names such as `int`,
`float`, `Person`, or `any[]`. It evaluates its argument once. `print` and
`string` format the payload; `len` accepts a boxed string or array. `int` and
`float` convert boxed numeric/string payloads using the normal conversion rules;
unlike `cast<int>`, `int` can convert a boxed float or parse a boxed string.

Pointer values cannot be boxed or extracted with `cast`, and `any` is rejected
in foreign signatures. A pointer to an ordinary `any` variable still follows
the normal borrow rules. Type erasure cannot conceal a local pointer escape.
Dynamic collections can form cycles through shared array storage; recursive
formatting/equality stops with a diagnostic after 128 nested `any` visits,
covering cycles and excessively deep acyclic values without overflowing the
native stack.

See [examples/dynamic.bc](examples/dynamic.bc) for a complete program.

## Generated C++ bridges

Use `extern "C++" fn` for a C++ free function or static member function:

```basicc
extern "C++" fn root(value: float) -> float = "demo::positive_root";
```

The optional quoted target is a qualified identifier (optionally starting with
`::`); when omitted, it defaults to the BasicC function name. The local BasicC
name remains unique even when several bindings select overloads of the same
C++ target. Arbitrary expressions, operator names, and explicit template
arguments are not supported. `extern "C" fn` is an alias for `extern fn`.

Signatures use the same ABI types and unsafe/lifetime rules as C declarations:
`int` maps to `int64_t`, `float` to `double`, `bool` to `bool`, and `c_int` to C++
`int`. Scalar pointers and `void` returns are supported. Reference parameters,
class instances, aggregate values, strings, callbacks, and instance methods are
not supported at this boundary. A function-pointer cast selects the exact
declared overload; mismatches fail during native compilation. An underlying
`noexcept` function is also accepted. Default arguments are not supplied.

Pass declaration headers with repeated `--cpp-header file` options, and
implementation sources, objects, or libraries with `--link file` / `-l name`.
Paths are relative to the shell's working directory. Headers are included in
the wrapper translation unit in option order; inline/header-only functions
need no separate implementation source. Header paths containing quotes or
newlines are rejected. For example:

```sh
build/basicc build examples/cpp/main.bc --cpp-header examples/cpp/native.hpp \
    --link examples/cpp/native.cpp -o build/cpp_demo
```

The driver compiles generated wrappers and linked `.cpp`, `.cc`, `.cxx`, or `.C`
sources as C++17 using `CXX` (default `c++`). C sources use `CC` and C11. Objects
retain link order, and the C++ driver supplies the C++ runtime when wrappers,
C++ headers, or C++ sources are present. For a C++ object/library exposing only
C symbols, supplying its declaration header with `--cpp-header` also selects
the C++ linker; object files alone do not identify their source language.

Wrappers have C linkage and catch all C++ exceptions before they cross generated
C code. A caught exception prints a diagnostic (including `what()` for
`std::exception`) and terminates the process with exit code 1. Pointer ownership
remains the caller's responsibility. BasicC checks finite floating-point return
values as it does for C calls.

`--emit-cpp file.bc [--cpp-header file]` prints wrappers and includes without
running a native compiler. `--emit-c` and `--emit-ir` contain wrapper declarations
and calls; their output must be linked with the separately compiled wrappers.
`--check` validates BasicC declarations but does not read C++ signatures from
headers. Both build backends compile and link wrappers automatically.

## Pointers and unsafe blocks

Append `*` to a type to describe a pointer. `&value` takes the address of mutable
storage; `*pointer` reads or writes that storage. Both operations require a
lexically enclosing `unsafe` block:

```basicc
fn increment(value: int*):
    unsafe:
        *value = *value + 1;

fn main() -> int:
    var value = 1;
    unsafe:
        var pointer: int* = &value;
        increment(pointer);
        print(*pointer);
    return 0;
```

Unsafe permission does not pass into a called function. That function needs its
own block for raw operations. Copying or comparing pointer values, including
shared comparisons, does not access memory and can occur outside an unsafe
block. Foreign calls always require one.

`null` is a null pointer literal and needs a pointer type context, such as
`var p: int* = null;` or `p == null`. Dereferencing a null pointer reports a
runtime error. There is no implicit pointer-to-boolean conversion.

Pointers may address scalars, structs, array descriptors, or mutable array
elements. For example, `int[]*` points to an array descriptor, and `(*person).age`
accesses a field through a struct pointer. Taking the address of a temporary or
an immutable string byte is rejected. Pointer arithmetic, pointer indexing,
pointer-to-pointer types, pointer fields, and arrays of pointers are not yet
supported. These restrictions keep the initial lifetime analysis explicit.

The checker tracks pointer assignments through a flow graph and checks it after
all functions have been analyzed. It rejects pointers to local storage escaping
through returns or writes to longer-lived variables, including updates inside
loops. Borrowed pointer parameters may be returned; the caller conservatively
assigns the result the shortest lifetime among its pointer arguments. Parameters
cannot be retained in globals. Aggregate pointer storage is disallowed so an
escape cannot be hidden inside an array or struct.

Global storage and arena-backed array elements live until process exit. A
pointer to an array element may therefore be returned even if the local array
descriptor has gone out of scope. A pointer to the descriptor itself may not.

This analysis is conservative: it combines all assignments to a pointer variable
and can reject code whose safety depends on branch conditions or overwriting an
earlier borrow. Unsafe blocks do not bypass these checks. Foreign memory remains
subject to the external contract described above.

See [examples/pointers.bc](examples/pointers.bc) for borrowed parameters, stable
array-element addresses, null comparisons, and a C function that updates a value.

## Compilation pipeline

The compiler performs lexing, parsing/import loading, semantic and lifetime
checking, and C lowering. That shared lowering preserves expression evaluation
order using temporary values and includes a small checked runtime. Two build
backends consume it:

```text
--backend c     checked C -> C compiler -> native executable
--backend llvm  checked C -> LLVM IR -> native object -> linked executable
```

The C backend is the default and uses `CC` (default `cc`). The LLVM backend
uses Clang for ABI lowering into LLVM IR and for compiling that IR to native
object code; `CC` (or `CXX` for C++ builds) performs final linking. `BASICC_CLANG` selects the Clang
executable, defaulting to `clang`. IR lowering still starts from generated C;
LLVM provides the optimizer and instruction selection.

Both backends accept `-O0` through `-O3`, with `-O0` the default. Optimizations
include those supplied by the selected toolchain, such as inlining, constant
folding, and removal of unreachable code. Type/lifetime checking runs first, and
integer overflow, bounds, null, and floating-point guards retain their specified
behavior. The compiler does not enable fast-math or unchecked arithmetic modes.

`--emit-ir file.bc [-O0|-O1|-O2|-O3]` writes LLVM IR to standard output without
linking. It contains typed values, basic blocks, runtime calls, and the local
target ABI. `--emit-c` exposes the shared C form. IR output requires Clang;
`--check`, `--tokens`, `--ast`, and `--emit-c` do not invoke a native compiler.

Broader ownership management remains a possible extension beyond the MVP.
See [GOALS.md](GOALS.md).
