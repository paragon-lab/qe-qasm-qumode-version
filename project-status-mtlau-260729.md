# Project Status (2026-07-29)
Mu-Te Lau

This file documents the status of the project, as of 2026-07-29, focusing on the
tasks I'm in charge of.

# Completed Features
- `qumode` declarations
- Basic qumode gates: displacement, SNAP, ECD; gate declaration syntaxes
    - See `docs/gates.md` for more details.
- Typed gate template parameters (`uint N` as array length) with call-site
  inference from array literals / declared named-array sizes and explicit
  `gatecall<N>(…)`; uninitialized named arrays reject as used-before-assigned.

# In Progress Features
- SNAP gate *body*: `for` in `GateOpList`, and `ctrl<i>` Fock-level control
  (template + sized array formals are done; see `docs/gates.md`)

# Gate-Level IR (Not Implemented)
Ended up not having time to implement it. To my defense, I deprioritized
this feature because:
- While examining what I had already implemented, I found out that the ASTs
  didn't contain enough information for the program level IR. Fixing that would
  be more important, so Yuchen wouldn't need to fix my mistakes.
- I decided that proper gate declaration syntaxes are more important to
  implement first. Without a type system, the parser cannot reject a lot of
  illegal gate calls, which, I believe, is more harmful than not having a
  gate-level IR before I leave.
- For single-qumode programs now, a DAG view that the gate-level IR would
  provide is not useful yet. I believe the S+D Unitary decomposition
  pass can be applied to the program level IR, and it wouldn't be too hard to
  migrate that pass to the gate level IR after we have that.

# Known Issues

- Apparently, [OpenQASM 3.0 has specified using curly braces for arrays](https://openqasm.com/language/types.html#arrays),
  but we're using square brackets. The square brackets are used in the OQ3
  standard to denote types, for example `int[32]` is a 32-bit integer, and
  `array[int[32], 5]` is a 5-element array of 32-bit integers.
  For this, the following options make the most sense to me:
  1. Keep current syntax, accept that both [] and <> denote
  typing in different contexts:
  ```qasm
  // [] denotes types and array literals, and <> denotes template parameters.
  // Do not use {} for array literals anywhere in the grammar.
  gate foo<uint N>(array[angle, N] alphas) qubit q {...}

  foo<3>([pi/2, pi/2, pi/2]) q;

  array[angle, 3] alphas = [pi/2, pi/2, pi/2];

  unitary u = [[1, 0], [0, 1]];
  ```
  This is arguably the most Python-like syntax, as it uses [] for both types and
  array literals. Python does not have template syntax, so we could argue that a
  different syntax here is acceptable.

  2. Change our syntax to fully comply with the OQ3 standard:
  ```qasm
  // [] denotes types, and {} denotes array literals.
  // Do not use <> anywhere in the grammar
  gate foo[uint N](array[angle, N] alphas) qubit q {...}

  foo[3]({pi/2, pi/2, pi/2}) q;

  // Specified by the OQ3 standard but not implemented in this codebase.
  array[angle, 3] alphas = {pi/2, pi/2, pi/2};

  // Note that unitary declarations would also be affected by this decision.
  unitary u = {{1, 0}, {0, 1}};
  ```
- Discovered several bugs/gaps in the codebase (see the issues on GitHub).
- SNAP remains opaque until gate-body `for` / `ctrl<i>` land; template array
  lengths work. See `gates.md`.
- If there are multiple gate declarations with the same name, the last one
  takes precedence. It is not clear if this is intended or a bug.
- There are a few AST nodes related to generic operands still using `Qubit`
  instead of `Operand` in their names. I decided to keep the legacy names for
  now because they seem to be less used.

# Some Other Notes
The things I'm not in charge of, but could be useful to note:

## Unitary / bumper feature gaps
- Lack of way to specify the precision of unitary decompositions
  (maybe `u.precision`?)
- What if `bumper` and `bumper_max` are simultaneously specified?

## Testing / tooling
- Unitary unit tests are not wired up to ctest. To wire them up, add tests in
  `tests/CMakeLists.txt`. I'd suggest adding both tests that should succeed and
  tests that should fail.
  There are plenty of examples of how to do this in `tests/CMakeLists.txt`.
- Unit tests only check whether the parser accepts/rejects input; they do not
  validate AST correctness. This is probably intentional as this repo does not
  contain any code that operates on the AST, but at some point we probably will
  need tests that validate AST correctness too.
