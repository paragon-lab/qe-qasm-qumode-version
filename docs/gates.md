# Gates

This document describes the syntax of qumode gates in our extension to
OpenQASM 3.0, as well as the limitations of the parser on these declarations.



# Basic Qumode Gates

At the moment, the following qumode gates are supported:
- displacement `disp(alpha) qm`
- SNAP: `snap([theta_0, theta_1, ...]) qm`
- ECD: `ecd(alpha) qb, qm`

## Displacement gate

Gate signature:
```qasm
gate disp(complex[float[64]] alpha) qumode qm;
```
The displacement gate is treated as a builtin gate. The user doesn't need to include
any file to use this gate.

## SNAP gate

Gate definition (WIP):
```
gate snap<uint N>(array[float[64], N] thetas) qumode qm {
    for i in [0:N] {
        ctrl<i> @ gphase(thetas[i]) qm; // controls the i-th Fock level
    }
}
```
Include `tests/include/cvgates.inc` to use this gate.

### Known Issues
The parser is not able to parse the above definition at the moment. As a result,
`snap` is currently implemented as an `opaque` gate. Essentially, the parser is
told that the definition of snap gate exists without actually seeing it.
Owing to this, the parser is not able to detect syntactic errors such as
```qasm
snap(pi/2, 0, 0.3) qm[0]; // missing array bracket [...]
snap([pi/2, 0, 0.3]) qm[0], qm[1]; // wrong number of operands
```
Since `opaque` gates are deprecated, the parser would throw warnings
upon parsing `opaque` SNAP gates. This is an issue that will be addressed in
in the future.

## ECD gate

Gate definition:
```
gate ecd(complex[float[64]] alpha) qubit ctrl, qumode target {
    negctrl @ disp(-alpha/2) ctrl, target;
    ctrl @ disp(alpha/2) ctrl, target;
}
```
Include `tests/include/cvgates.inc` to use this gate.



# Gate Declaration Syntax

These qumode gates introduce new types of parameters and operands. In
OpenQASM 3.0, all parameters and operands are of type `angle` and `qubit`
respectively. This is no longer the case in our extension. In particular,
- SNAP gate introduces using an array of angles as a parameter, and this array
  is variable in length.
- Displacement and ECD gates introduce using a complex number as a parameter.
- All gates introduce using qumodes as operands.
These extensions necessitate a proper extension of the gate declaration syntax
that facilitates type checking at the syntactic level.

Currently, the supported classical types for parameters are `angle`, `float[N]`,
`complex[float[N]]`, and fixed-length arrays of these types. This suffices to
implement ECD gates and allow some flexibility in defining custom gates.
Type checking for typed gate declarations are enforced both in definitions and
in call sites. The syntax needed to implement SNAP gate is not yet implemented.

## Examples for the new syntax

The main addition to the syntax is that the types of parameters and operands are
specified before the name of the parameter or operand (à la C). For example,
the following is a valid gate declaration:
```
gate ecd(complex[float[64]] alpha) qubit c, qumode t {
    negctrl @ disp(-alpha/2) c, t;
    ctrl @ disp(alpha/2) c, t;
}
```

Arrays of `angle`s, `float[N]`s, or `complex[float[N]]`s can also be used as
parameters:
```
gate foo(array[angle, 3] phases) qubit q {
    ctrl @ gphase(phases[0]) q;
    ctrl @ gphase(phases[1]) q;
    ctrl @ gphase(phases[2]) q;
}
gate bar(array[complex[float[64]], 3] alphas) qumode qm {
    disp(alphas[0]) qm;
    disp(alphas[1]) qm;
    disp(alphas[2]) qm;
}
```

It is also possible to mix and match different types of parameters and operands:
```
gate baz(array[complex[float[64]], 3] alphas, angle beta) qubit qb, qumode qm {
    ctrl @ gphase(beta) qb;
    ecd(alphas[0]/2) qb, qm;
    ecd(alphas[1]/3) qb, qm;
    ecd(alphas[2]/4) qb, qm;
}
```

The above syntaxes are already implemented in the parser. However, SNAP gates
cannot yet be parsed because its definition requires further syntax extensions,
as we discuss below.

## WIP: SNAP gates; Variable-length arrays with compile-time known length

Here, we describe the syntax needed for gates with variable-length arrays with
compile-time known length.
For example, consider the following definition of the SNAP gate:
```
gate snap<uint N>(array[float[64], N] thetas) qumode qm {
    for i in [0:N] {
        ctrl<i> @ gphase(thetas[i]) qm; // controls the i-th Fock level
    }
}
```

To enable this syntax, we still need to support the template parameter syntax,
the `for` loop in gate bodies, and the `ctrl<i>` syntax that controls the i-th
Fock level.
The length of the array `thetas` must be compile-time
known. The user would be able to leave out the template parameters:
```
snap([pi/2, 0, 0.3]) qm[0];
```
and let the parser/compiler infer the length of the array (in this case, 3).
Alternatively, if the user specifies the template parameters,
```
snap<3>([pi/2, 0, 0.3]) qm[0];
```
the compiler would be able to check whether the length of the array is 3 and
throw an error if it is not.

## Compatibility with OpenQASM 3.0 gate definitions

Since OpenQASM 3.0 is widely adopted, it would be desirable to still be able to
parse `OpenQASM 3.0` gate declarations. For example, consider this declaration
of the `rz` gate:
```
gate rz(theta) q {
    ctrl @ gphase(theta) q;
}
```
Under the new typed syntax, this declaration would be illegal, requiring the
users to rewrite their gate declarations.
To alleviate this issue, we allow gate definitions with no typed parameters and
operands to be accepted. In this case, all parameters would be
treated as `angle`s and operands would be treated as `qubit`s. That is, the
above definition would be treated as the following:
```
gate rz(angle theta) qubit q {
    ctrl @ gphase(theta) q;
}
```
To avoid footguns, the untyped and typed gate declaration syntaxes cannot be
mixed with each other. As soon as one parameter or operand is typed,
all other parameters and operands must also be typed.
```qasm
gate ecd(complex[float[64]] alpha) qubit c, qumode t {...} // ok
gate ecd(alpha) qubit c, qumode t {...} // error, alpha is not typed
gate ecd(complex[float[64]] alpha) c, qumode t {...} // error, c is not typed
```



# AST representation

This section describes how the AST representation of gates works. If you are not
a compiler developer, you can skip this section.

## The `<Gate/>` node

The AST representation of a gate declaration is enclosed in the
`<GateDeclarationNode/> --> <Gate/>` node. Inside the `<Gate/>` node, we have
- `<Name/>`: the name of the gate.
- `<MangledName/>`: the mangled name of the gate. The mangling logic resides in
  `lib/AST/ASTMangler.cpp`.
- `<Opaque/>`: whether the gate is opaque. If the gate is opaque, the parser is
  told that the definition of the gate exists without actually seeing it. This
  is similar to how in C/C++, we can declare a function or `extern` a variable,
  without the parser seeing its definition. This option is technically deprecated.
- `<GateCall/>`: whether the gate is a gate call. This would be false at
  a gate declaration, and true at a gate call.
- `<FullyTyped/>`: whether the gate is fully typed. If the gate is fully typed,
   `<FormalParamTypes/>` and `<FormalQuantumTypes/>` would be present.
- `<FormalParamTypes/>`: the types of the gate parameters.
- `<FormalQuantumTypes/>`: the types of the gate quantum operands.
- `<Params/>`: the parameters of the gate.
  Each parameter can be a `<Angle/>`, a `<Float/>`, a `<MPComplex/>`,
  or a fixed-length array of these types.
- `<Operands/>`: the operands of the gate. Each operand can be a `<Qubit/>` or a `<Qumode/>`.
  NOTE: In the original OpenQASM 3.0 parser, this field was called `<Qubits/>`.
- `<GateQOpList/>`: the list of operations in the gate body.

## How Gate declarations and calls are represented

For definitions, the parser sees a gate declaration and creates a
`<GateDeclarationNode/>` node with a `<Gate/>` node inside and populates the
fields accordingly.

For calls, this `<Gate/>` node would be enclosed in the
`<GenericGateOpNode/>/<GateOpNode/>`.

Loosely speaking, when the parser sees a gate call, it first checks if the
definition of the gate exists either as a builtin gate, a declared gate, or an
opaque gate.
- For builtin gates, the parser builds a `<Gate/>` for that gate.
- For declared and opaque gates, the parser clones the `<Gate/>` node from the
  definition, and replaces the fields in `<Params/>` and `<Operands/>` with
  the actual values from the call site.
- A quirk is that the parser does **NOT** substitute the parameters and operands
  with the actual values in the gate body (the contents of `<GateQOpList/>`).
  The parameters in the body still need to be substituted with what the call
  site provides. It seems like this is left to the compiler.
  This does also mean that a gate call node can carry redundant information
  if the body is never used (e.g., if the gate is considered a basis gate).
