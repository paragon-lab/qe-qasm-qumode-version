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

## Design rules

- **All-untyped** classical + bare quantum formals = legacy OpenQASM 3 (`ProductionRule_1430` / `1431`). No call-site type checks.
- **Any typed classical formals** with **typed quantum operands** = fully typed (`ProductionRule_10030`). No partial typing.
- Templates / SNAP array formals deferred. Opaque `snap(thetas)` stays untyped until then.
- `ctrl` is a reserved token (`TOK_CTRL`); do not use `ctrl` as an operand name.
- **Out of CV-core scope:** `for`/`while` in `GateOpList`; int/bool/duration/bit array formals.

## Grammar / production

| Piece | Location |
|-------|----------|
| Typed quantum list | `GateTypedQuantumOperandList` in `lib/Parser/QasmParser.y` |
| Decl production | `ProductionRule_10030` in `ASTProductionFactory.cpp` |
| Shared AST build | Delegates to `ProductionRule_1430` after validating formals |
| Call entry | `ProductionRule_3500` → `CreateQOpNodeCall` → `CreateGateCall` |
| Compatibility | `ASTGateType` in `include/qasm/AST/ASTGateType.h` |

Decl-time checks in `10030`:
- Classical: `IsExplicitClassicalGateParamType` (includes explicit `angle`,
  float/complex/arrays; bare Identifier formals are also angles and allowed
  on this path when quantum operands are typed)
- Quantum: polymorphic type must be `ASTTypeQubit` or `ASTTypeQumode`
- Stores `FormalParamTypes`, `FormalParamArraySizes`, `FormalQuantumTypes`

## GateType lattice (call-site)

`FormalParamTypes[i]` (+ optional size) is the type oracle. Carriers (`Params` / `ArrayParams` / `ComplexArrayParams` / `ComplexParams`) are storage only.

`ASTGateType::Compatible(F, A)`:
- Real↔angle array family (`AngleArray` / `FloatArray` / `MPDecimalArray`)
- Real→complex scalar and real-array→complex-array promotion
- Reject complex→real (scalar or array)
- Array size equality when both sides known

Hook: `ASTTypeDiscovery::ValidateTypedGateCall` from `CreateGateCall` **before** `CloneCall`, only if `GN->IsFullyTyped()`.

## AST storage (definition gate)

| Formal kind | Storage | Read for checking |
|-------------|---------|-------------------|
| `complex[…]` | `ComplexParams` | Prefer `FormalParamTypes[i]` on fully-typed gates |
| other scalars | coerced into `Params` (angles) — **original type lost** | Must use `FormalParamTypes` |
| `array[float/angle/mpdecimal, N]` | Declared STE in GSTM + optional `.gatearray` angle alias in `ArrayParams` | `FormalParamTypes` + size; body `thetas[i]` uses float STE |
| `array[complex[…], N]` | `ComplexArrayParams` (declared STE; no angle view) | `FormalParamTypes` + size |
| qubit / qumode | `QCParams` STE; polymorphic type restored | Prefer `FormalQuantumTypes[i]` |
| `Qubits` vector | synthetic GateQubitParam-shaped nodes | **Do not** use for qubit vs qumode |

Array formals share one ctor path in `ASTGates.cpp`: erase LSTM, bind declared STE into GSTM, then push angle-family into `ArrayParams` or complex into `ComplexArrayParams`.

## Literals (`ProductionRule_10010`)

`ASTGateType::ExpressionListHasComplex` decides complex vs angle-array literal. Call-site and builder share that classification.

Gate call `ArgsList` is `'(' ExprList ')'`. Array literals are `ExprList`
elements (`[ExprList]`), so `foo([a,b], theta)` works alongside `… im`
complex initializers and nested function calls. Do not reintroduce a
separate `GateCallArg` list — it LR-conflicts with function-call parsing.

## Do not

- Check untyped OQ3 gates.
- Treat “has `ComplexParams` alone” as fully typed (classical-only typing + bare qubits is not the fully-typed form).
- Leave stray `std::cerr` / `std::cout` debug prints in production paths.

## Array formals (SNAP-like)

Gate param lists provisionally type identifiers as `ASTTypeAngle`. `ProductionRule_822` must **rebind** Angle/Undefined → array type and allow formals in gate/function contexts (`AllowArrayInCurrentContext` / `IsGateParameterArgument`). Do not leave hard `assert(Id->GetSymbolType() == Ty)` — that SIGABRTs on `array[float[64], N] thetas`.

`GateOpList` does **not** include `for` loops yet; SNAP-style loop bodies need grammar work separately.

## Tests

- Positive: `tests/src/qumode/ecd-mixed.qasm` (`ecd(0.5) qb, qm`)
- Positive: `tests/src/qumode/complex-array-param.qasm` (`array[complex[…], N]` formal + call)
- Positive: `tests/src/qumode/float-array-param.qasm` (`array[float[64], N]` body index + call)
- Positive: `tests/src/qumode/angle-array-param.qasm` (`array[angle, N]` body index + multi-gate ASTM reuse)
- Positive: `tests/src/qumode/multi-angle-array-param.qasm` (two angle arrays, different `N`)
- Positive: `tests/src/qumode/angle-scalar-param.qasm` (`angle` scalar + float array formals)
- Negative (expect-fail `test $? -ne 0`):
  - `ecd-param-reject.qasm` — angle array for complex formal
  - `ecd-operand-reject.qasm` — qumode where qubit expected
  - `ecd-operand-order-reject.qasm` — swapped qubit/qumode
  - `disp-qubit-reject.qasm` — builtin disp pattern
  - `array-elem-complex-reject.qasm` — complex element in float array formal
  - `array-size-mismatch-reject.qasm` — literal length ≠ formal `N` (param 0)
  - `array-size-mismatch-param1-reject.qasm` — size mismatch on second array formal
- Include: `tests/include/cvgates.inc`
- Manual: `gate-decl.qasm` (`rz3` + `disp3`)
