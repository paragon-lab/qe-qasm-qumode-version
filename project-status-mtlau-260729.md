# Project Status (2026-07-29)
Mu-Te Lau

This file documents the status of the project, as of 2026-07-29, focusing on the
tasks I'm in charge of.

# Completed Features
- `qumode` declarations: mirrors qubit declaration syntaxes
  ```qasm
  qumode qm;
  qumode[3] qms;
  ```
- Basic qumode gates: displacement, SNAP, ECD gates
    - `disp(alpha) qm;`
    - `snap[N]([theta_0, theta_1, theta_2,...]) qm;`
    - `ecd(alpha) qb, qm;`
    - A type system is implemented along with the gate declaration syntaxes.
    `docs/gates.md` lists the types of errors that the type system can catch.
- Gate declaration syntaxes: see `docs/gates.md` for more details.
- Typed gate template parameters (`uint N` as array length) with call-site
  inference from array literals / declared named-array sizes and explicit
  `gatecall[N](…)`; uninitialized named arrays reject as used-before-assigned.
- Fock-level `ctrl[…]` / `negctrl[…]` (distinct from qubit `ctrl(n)`): level may
  be an integer, identifier, or expression (`ctrl[3]`, `ctrl[N]`, `ctrl[N-1]`);
  see `docs/gates.md`.
- We proposed using angle brackets for template parameters. I decided to use
  square brackets instead to avoid parsing issues. See `docs/gates.md` for more
  details.

# In Progress Features
- SNAP gate *body*: `for` in `GateOpList` (Fock `ctrl[i]` / `ctrl[N-1]` is done;
  template + sized array formals are done; see `docs/gates.md`)

# Gate-Level IR (Not Implemented)
Ended up not having time to implement it.
I prioritized other features and fixes because:
- While examining what I had already implemented, I found out that the ASTs
  didn't contain enough information for the program level IR. Fixing those would
  be more important, so others wouldn't need to fix my mistakes.
- I decided that a proper type system and gate declaration syntax are
  more important to implement first. Without these, the parser would be very
  fragile, not being able to catch a lot of illegal gate calls.
- For single-qumode programs now, the DAG view that the gate-level IR would
  provide is not useful yet. I believe the S+D Unitary decomposition pass  
  can be applied to the program level IR, and it wouldn't be too hard to migrate
  that pass to the gate level IR after we have that.

# Known Issues

### Brackets

Apparently, [OpenQASM 3.0 has specified using curly braces for arrays](https://openqasm.com/language/types.html#arrays),
but we're using square brackets. The square brackets are used in the OQ3
standard to denote types, for example `int[32]` is a 32-bit integer, and
`array[int[32], 5]` is a 5-element array of 32-bit integers.

Separately, using angle brackets prove to be problematic for parsing.
For example, suppose we want to support expressions like `ctrl<N-1>` or
`ctrl<3*2>`, we would have a rule like `TOK_CTRL '<' Expr '>' ...`.
But `'>'` can also be a part of `Expr`, making it very annoying to parse.
Currently, we disallow expressions inside angle brackets to avoid this problem.
Granted, the user might not have an incentive to put expressions inside angle
brackets, but if they do, it would be hard to support that.

Going forward, I think we have the following options:
1. Use square brackets for everything:
```qasm
// [] denotes types, template parameters, and array literals.
// No {} for anywhere in the grammar.
gate foo[uint N](array[angle, N] alphas) qubit q {...}

foo[3]([pi/2, pi/2, pi/2]) q;

// Would need to change existing parse rules to support this.
array[angle, 3] alphas = [pi/2, pi/2, pi/2];
```
This makes the syntax more Python-like but inconsistent with the OQ3
standard. We might be using square brackets for too many things.

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
This makes our syntax fully compliant with the OQ3 standard, though using `{}`
for array literals may be less intuitive for users---Using `{}` for initializers
seems to be more common in system-level programming languages.

### Other known issues
- Discovered several bugs/gaps in the codebase (see the issues on GitHub).
- SNAP remains opaque until gate-body `for` lands; Fock `ctrl[]` (including
  level expressions like `N-1`) and template array lengths work. See `gates.md`.
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
