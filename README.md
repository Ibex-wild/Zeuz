<img src="zeuz.webp" alt="Zeuz logo" width="200">
# Zeuz Language Reference

Draft reference for the Zeuz programming language, version 1.0.

Zeuz is a small statically-typed, imperatively-structured, procedural
language. The reference implementation is an ahead-of-time compiler
written in C++17 with LLVM as the code generation backend. Source files
carry the `.zz` extension and are translated to a native binary before
execution. There is no interpreter and no JIT.

## Contents

- `README.md` — this file.
- `index.html` — the full reference rendered as plain HTML5, with no
  CSS, no JavaScript, and no external dependencies. Open it in any
  browser, or serve it from any static file server.

## Status

Draft. The document identifier is `ZEUZ-REF-1.0`. The reference MAY be revised without notice. See
Section 0 of the reference for details on the RFC 2119 conformance
keywords and the change log.

## What the Reference Covers

- Lexical structure: source encoding, whitespace, comments, tokens,
  identifiers, keywords, reserved words, literals, operators, and
  punctuators.
- Grammar in Extended Backus-Naur Form, with numbered productions.
- Types and the type system, including the deliberate collapsing of
  integer type aliases to a single 64-bit signed representation.
- Expressions and statements, with semantics and examples for each.
- Scoping and name resolution rules.
- Built-in functions: `print`, `len`, `input`, `assert`.
- The memory model, including the stack-and-leak strategy.
- Program structure and execution, including the `entryf` requirement.
- Undefined behavior, implementation-defined behavior, and a rationale
  chapter explaining the design choices.
- Appendices: grammar index, reserved words, precedence table, example
  programs, and differences from C.

## Viewing the Reference

Open `index.html` in a web browser. The document uses only semantic
HTML5 tags (`h1`–`h4`, `p`, `ul`, `ol`, `pre`, `code`, `strong`, `em`)
and has no styling. It renders legibly in any user agent and prints
cleanly.

To serve it locally:

    python3 -m http.server 8000

Then visit `http://localhost:8000/index.html`.

## Building the Reference Compiler

The reference compiler is not part of this repository. This repository
contains only the language reference. See the compiler repository for
build instructions.

## Conformance

An implementation conforms to the reference if it accepts every
well-formed program, rejects every ill-formed program with a
diagnostic, and documents every implementation-defined behavior listed
in Section 13.5. Section 4 defines conformance in full.

## Reserved Keywords

The reference lists reserved keywords in Section 5.7 and
Appendix B. These keywords are lexed but have no meaning in version
1.0. A source file that uses any of them in any position is
ill-formed. The keywords are reserved for a future module system,
error handling, pattern matching, object-orientation, concurrency,
pointer syntax, and a number of smaller features. See Section 15.1 for
the current priorities.

## Known Limitations

The reference documents the following limitations of version 1.0
in Section 12.7 and Section 14:

- No garbage collector. Strings allocated by concatenation, cast, or
  `input` are never freed.
- No pointer arithmetic and no explicit pointer type in the source
  language.
- All integer type aliases (`i8`, `u32`, `usize`, and so on) collapse
  to a single 64-bit signed integer. Width and signedness are not
  enforced.
- Array parameters do not carry a length. The caller MUST supply the
  length as an additional parameter.
- No generic types, closures, or first-class functions.
- No module system. All program text MUST appear in a single source
  file.
- No error recovery in the parser. The first error terminates
  compilation.
- No debug information. Runtime crashes do not report source line or
  column.
- `==` on strings compares byte sequences via `strcmp`, not pointer
  identity. This is documented explicitly in Section 8.6.8.

## License

Copyright 2026 Zeuz language

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

## Contributing

Comments on the reference are accepted as issues in the compiler
repository. Changes that affect normative behavior require an update
to the change log in Section 0.5.
