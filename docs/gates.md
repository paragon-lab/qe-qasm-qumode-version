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
        ctrl(i) @ gphase(thetas[i]) qm;
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
Gate definition (WIP):
```
gate ecd(complex[float[64]] alpha) qubit ctrl, qumode target {
    negctrl @ disp(-alpha/2) ctrl, target;
    ctrl @ disp(alpha/2) ctrl, target;
}
```
Include `tests/include/cvgates.inc` to use this gate.

## Known Issues
Currently, the gate definition in the `.inc` file lacks the typing of parameters
and operands, because those are not supported yet. As a result, the parser is
not able to detect syntactic errors such as
```qasm
ecd(0.3 + 0.5 im) qm, qb; // wrong order of operands: qubit should come before qumode
```

# Gate Declaration Syntax (WIP)
To fix the above limitations would require a proper extension of the gate f
declaration syntax. Generally speaking, introducing `qumode`s, `disp`, `snap`,
and `ecd` gates would require the following extensions:
- Gate parameters should be typed. In OPENQASM 3.0, all parameters are floating
  point numbers. In our extension, they can also be an array of floating point
  numbers or complex numbers.
- Gate operands should be typed. In OPENQASM 3.0, all operands are qubits.
  In our extension, they can also be qumodes.
- Ideally, we should still be able to parse `OPENQASM 3.0` gate definitions.

A complete gate declaration syntax might look like the following:
```
GATE_DECLARATION :== gate IDENTIFIER(<TEMPLATE_PARAMS>)? (\(PARAMS\))? OPERANDS { GATE_BODY };

TEMPLATE_PARAMS :== TEMPLATE_PARAM (',' TEMPLATE_PARAM)*;
TEMPLATE_PARAM :== CLASSICAL_TYPE IDENTIFIER;

PARAMS :== PARAM (',' PARAM)*;
PARAM :== CLASSICAL_TYPE? IDENTIFIER;

OPERANDS :== OPERAND (',' OPERAND)*;
OPERAND :== QUANTUM_TYPE? IDENTIFIER;

GATE_BODY :== GATE_STATEMENT*;
```

## Example 1: defining SNAP gate:
```
gate snap<uint N>(array[float[64], N] thetas) qumode qm {
    for i in [0:N] {
        ctrl(i) @ gphase(thetas[i]) qm;
    }
}
```
At least for the moment, the length of the array `thetas` must be compile-time
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

## Example 2: defining ECD gate:
```
gate ecd(complex[float[64]] alpha) qubit ctrl, qumode target {
    negctrl @ disp(-alpha/2) ctrl, target;
    ctrl @ disp(alpha/2) ctrl, target;
}
```
We can see why typing is important with this example. Without the typing of
`alpha` as a complex number, the parser wouldn't know whether the `0.3` is a
real number (float[64]) or a complex number (complex[float[64]]).
```qasm
ecd(0.3) qb, qm; // would be parsed as a float[64]
```
On the other hand, without the typing of the operands as `qubit` and `qumode`,
the parser couldn't catch the following errors:
```qasm
ecd(0.3 + 0.5 im) qm[0], qb[0];
ecd(0.3 + 0.5 im) qb[0], qb[1];
ecd(0.3 + 0.5 im) qm[0], qm[1];
```

## Compatibility with OPENQASM 3.0 gate definitions
It might be desirable to still be able to parse `OPENQASM 3.0` gate
definitions. For example, the user might have a OPENQASM 3.0 qubit gate
definition from some other sources:
```
gate rz(theta) q {
    ctrl @ gphase(theta) q;
}
```
We might want to be able to still understand this definition, so that the user
wouldn't have to rewrite their existing OPENQASM 3.0 gate definitions.
Practically, this means that the user would be able to leave out the typing of
parameters and operands. Where the typing is omitted, the parser would assume
parameters are `float[64]`s and operands are `qubit`s, so the above
definition would be identical to
```
gate rz(float[64] theta) qubit q {
    ctrl @ gphase(theta) q;
}
```
fs
### Potential footgun
A possible scenario where this becomes a footgun is that the user might not
be aware of the correct default type mechanism and write something like
```
gate ecd(alpha) qubit ctrl, qumode target {
    ...
}

qubit qb; qumode qm;
ecd(0.3 + 0.5 im) qb, qm; // error; expected float[64]
                          // Might also be a rather hideous error message, e.g.,
                          // "Error: expected ')' but got '+';
```

Default type mechanism has been known as a footgun in C and had to be
retroactively banned. We could argue that we should just require the users to
modify their existing OPENQASM 3.0 includes (maybe providing a script to do that
automatically).
