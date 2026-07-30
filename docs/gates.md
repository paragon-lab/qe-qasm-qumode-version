# Qumode gates
This document describes the syntax of qumode gates in OpenQASM 3.0, as well as
the current limitations of the parser on these declarations.

At the moment, the following qumode gates are supported:
- displacement `disp(alpha) qm`
- SNAP: `snap([theta_0, theta_1, ...]) qm`
- ECD: `ecd(alpha) qb, qm`

# Displacement gate
Gate signature:
```qasm
gate disp(complex[float[64]] alpha) qumode qm;
```
Displacement gate is treated as a builtin gate. The user doesn't need to include
any file to use this gate.

# SNAP gate
Gate definition (WIP):
```
gate snap<uint N>(array[float[64], N] thetas) qumode qm {
    for i in [0:N] {
        ctrl<i> @ gphase(thetas[i]) qm; // controls the i-th Fock level
    }
}
```
Include `tests/include/cvgates.inc` to use this gate.

## Known Issues
The parser is not able to parse the above definition at the moment. As a result,
`snap` is currently implemented as an `opaque` gate. Essentially, the parser is
told that the definition of snap gate exists without actually seeing it.
Owing to this, the parser is not able to detect syntactic errors such as
```qasm
snap(pi/2, 0, 0.3) qm[0]; // missing array bracket [...]
snap([pi/2, 0, 0.3]) qm[0], qm[1]; // wrong number of operands
```
Also owing to using the `opaque` mechanism, the parser would throw warnings
upon parsing SNAP gates.

# ECD gate
Gate definition:
```
gate ecd(complex[float[64]] alpha) qubit ctrl, qumode target {
    negctrl @ disp(-alpha/2) ctrl, target;
    ctrl @ disp(alpha/2) ctrl, target;
}
```
Include `tests/include/cvgates.inc` to use this gate.

# Gate Declaration Syntax
These qumode gates introduces new types of parameters and operands. In
OPENQASM 3.0, all parameters and operands are of type `angle` and `qubit`
respectively. This is no longer the case in our extension. In particular,
- SNAP gate introduces using an array of angles as a parameter, and this array
  is variable in length.
- Displacement and ECD gates introduce using a complex number as a parameter.
- All gates introduce using qumodes as operands.
These extensions necessitate a proper extension of the gate declaration syntax
that facilitates type checking at the syntactic level.

## Implemented
Roughly speaking, our new gate declaration syntax looks like the following:
```
GATE_DECLARATION :== gate IDENTIFIER(<TEMPLATE_PARAMS>)? (\(PARAMS\))? OPERANDS { GATE_BODY };

TEMPLATE_PARAMS :== TEMPLATE_PARAM (',' TEMPLATE_PARAM)*;
TEMPLATE_PARAM :== CLASSICAL_TYPE IDENTIFIER;

PARAMS :== PARAM (',' PARAM)*;
PARAM :== CLASSICAL_TYPE IDENTIFIER;

OPERANDS :== OPERAND (',' OPERAND)*;
OPERAND :== QUANTUM_TYPE IDENTIFIER;

GATE_BODY :== GATE_STATEMENT*;
```
Currently, the supported classical types for parameters are `angle`, `float[N]`,
`complex[float[N]]`, and fixed-length arrays of these types. This suffices to
implement ECD gates and allow some flexibilities in defining custom gates.
Type checkings for typed gate declarations are enforced both in definitions and
in call sites. The syntax needed to implement SNAP gate is not yet implemented
(See below).

## Variable-length arrays with compile-time known length (WIP)
Here, we describe the syntax needed for gates with variable-length arrays with
compile-time known length.
For example, consider the fsfollowing definition of the SNAP gate:
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


## Compatibility with OPENQASM 3.0 gate definitions
Since OPENQASM 3.0 is widely adopted, it would be desirable to still be able to
parse `OPENQASM 3.0` gate declarations. For example, consider this declaration
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
To avoid footguns, the untype and typed gate declaration syntax cannot be mixed
with each other. As soon as one parameter or operand is typed,
all other parameters and operands must also be typed.
```qasm
gate ecd(complex[float[64]] alpha) qubit c, qumode t {...} // ok
gate ecd(alpha) qubit c, qumode t {...} // error, alpha is not typed
gate ecd(complex[float[64]] alpha) c, qumode t {...} // error, c is not typed
```
