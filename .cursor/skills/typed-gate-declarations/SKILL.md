---
name: typed-gate-declarations
description: >-
  Fully-typed OpenQASM gate declarations (qubit/qumode formals + explicit
  classical types) and call-site type checking in this fork. Use when editing
  gate grammar, ProductionRule_10030, CreateGateCall, ValidateTypedGateCall,
  ASTGateType, cvgates.inc, ECD/disp/SNAP typing, or when adding typed gate
  call checks.
---

# Typed gate declarations

Syntax and the frozen `disp` / `snap` / `ecd` call ABI are in `docs/gates.md`. This file is only the invariants that are easy to break.

## Which production

- Untyped classical formals and bare quantum operands: `ProductionRule_1430` / `1431`. No call-site checks.
- Any typed classical formal together with typed quantum operands: `ProductionRule_10030` only. No partial typing. It delegates to `1430` after validation.
- `uint` templates used as array lengths: `ProductionRule_10031`. Call-site `foo([…])` infers `N`; `foo[N]([…])` checks. Do not overwrite body `N`.
- Calls: `ProductionRule_3500` → `CreateQOpNodeCall` → `CreateGateCall`. `ValidateTypedGateCall` runs before `CloneCall`, and only when `IsFullyTyped()`.

`FormalParamTypes` and `FormalQuantumTypes` are the type oracle. `Params` and the `Operands` vector are storage. Do not use `Operands` to tell qubit from qumode. Untyped calls still store angle-shaped `Params`.

## Do not break

- `ArgsList` is `'(' ExprList ')'`. A separate `GateCallArg` list steals the Identifier `'('` state from function calls.
- `ctrl` is `TOK_CTRL`. Do not use it as an operand name. Fock levels are `FockLevelExpr` (`ArithExpr`), including `ctrl[N-1]`. That is distinct from qubit `ctrl(n)`.
- `ProductionRule_822` rebinds a provisional `ASTTypeAngle` formal to the array type. A hard `assert` on the symbol type aborts on `array[float[64], N]`.
- After an `array[…]` formal, `SetCurrentType(ForStatement)` must win over leftover array `PreviousType`, or the induction variable is typed as an array.
- Real↔angle arrays promote. Complex→real does not. Array sizes must match when both are known.
- No `std::cerr` / `std::cout` debug prints on these paths.

## Out of scope

`while` in `GateOpList`. int/bool/duration/bit array formals. Template params that are not `uint` sizes.

## Tests

`tests/src/qumode/` and `tests/CMakeLists.txt`. Include dir `tests/include` (`cvgates.inc`). New tests need a `CMakeLists.txt` entry and a reconfigure, or CTest will not see them.
