# Project Status (2026-07-29)
Mu-Te Lau

This file documents the current status of the project, as of 2026-07-29.

# Completed Features
- `qumode` declarations
- Basic qumode gates: displacement, SNAP, ECD
    - See `docs/gates.md` for more details.
- `unitary` type; specifying bumper states
- Gate operand AST (`Operands`, `OperandParams`, `GateOperandParam`, qubit vs
  qumode nodes). See **AST representation** in `docs/gates.md`.
- Gate-call expression fidelity for complex params (cluster A): BinaryOp/UnaryOp
  args on `disp` (and other complex-involving exprs) keep an
  `ASTComplexExpression` under `MPComplex::Expr` instead of folding through
  angle → real+0i. Example: ECD body `disp(alpha/2)` dumps `alpha` `/` `2`.
- Typed gate-call carriers (cluster B): when `FormalParamTypes` (or builtin
  `disp`) expects complex, real literals / reals / BinaryOp/UnaryOp are stored
  as `MPComplex` directly (`0.5` → `0.5+0i`), not as angle `double0`. No
  separate ImplicitCast node.
- Note: call-site nested `GateQOpList` is a dump/representation quirk (not
  unrolling); see **AST representation** in `docs/gates.md`.

# In Progress Features
- Assigning a matrix to a `unitary` variable; checking if it is a valid unitary
- Syntax elements that would support gate declarations of SNAP gates

# Known Issues

## Typed-gate representation debt
- Typing system is work in progress. Currently, only gates with a fixed number of
  parameters can be defined. This includes ECD but not SNAP. See `gates.md`.
- There are a few AST nodes related to generic operands still using `Qubit`
  instead of `Operand` in their names. I decided to keep the legacy names for
  now because they seem to be less used.

## Unitary / bumper feature gaps
- Lack of way to specify the precision of unitary decompositions
  (maybe `u.precision`?)
- What if `bumper` and `bumper_max` are simultaneously specified?

## Testing / tooling
- Unitary unit tests are not wired up to ctest.
- Unit tests only check whether the parser accepts/rejects input; they do not
  validate AST correctness. (May be intentional for now.)
