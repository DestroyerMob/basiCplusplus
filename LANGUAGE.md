# BasicC Language Contract (v0.2)

BasicC is intended to be a statically compiled systems language with C-like
performance and interoperability, safer defaults, and scripting-language
ergonomics. This document covers the lexical and syntactic contract implemented
by the first two compiler milestones.

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
`extern`, `unsafe`, `struct`, `true`, and `false`. Fixed-width type names such
as `i32` and the future dynamic type `any` remain ordinary identifiers until
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
it does not create a dynamically typed value. The future `any` type will make
runtime typing explicit. Raw pointers and unchecked operations will require an
`unsafe` context.

## Implemented syntax

Functions use typed parameters. Their return type may be explicit or left for
the future type checker to infer:

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

`import`, `extern`, `unsafe`, and `struct` are reserved for later milestones and
currently produce an unsupported-syntax diagnostic.

## Compilation direction

The completed compiler will progress from lexing through parsing, semantic and
lifetime checking, an intermediate representation, native code generation, and
system linking. C ABI support comes before generated C++ bridge support. Neither
semantic analysis, code generation, nor interoperability is part of v0.2.
