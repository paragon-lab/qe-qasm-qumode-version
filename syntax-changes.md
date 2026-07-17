This issue documents the syntax changes from the OPENQASM 3.0 specification and
the reasons.

# (WIP) File Header
OPENQASM files always start with a `OPENQASM` header:
```bison
// Original
// Example: `OPENQASM 3.0;`
version: OPENQASM VersionSpecifier SEMICOLON;
```
We define our own header to differentiate our input language from OPENQASM.
The word `SEQCQASM` is kinda a placeholder---we can discuss what it should be.
```bison
// Updated
// Example: `SEQCQASM 3.0;`
SEQCQASM: "SEQCQASM";
version: SEQCQASM VersionSpecifier SEMICOLON;
```

# Quantum Resource Declaration
We'd like to support `qumode` as a distinct resource type alongside `qubit`.
For example:
```qasm
qubit qb;
qubit[5] qbs;
qumode qm;
qumode[7] qms;
```

To support this, we added `qumode` as a token:
```bison
// lib/Parser/QasmParser.y
%token <String> TOK_QUMODE
```
We extended the declaration statement grammar as follows:
```bison
// lib/Parser/QasmParser.y
Decl
   : ...
  | TOK_QUMODE Identifier ';' {
    $$ = ASTProductionFactory::Instance().ProductionRule_10000(GET_TOKEN(2), $2);
  }
  | TOK_QUMODE '[' Integer ']' Identifier ';' {
    $$ = ASTProductionFactory::Instance().ProductionRule_10001(GET_TOKEN(5), $5, $3);
  }
  | TOK_QUMODE '[' Identifier ']' Identifier ';' {
    $$ = ASTProductionFactory::Instance().ProductionRule_10002(GET_TOKEN(5), $5, $3);
  }
```

# Gate Calls
We need to minimally support the following new gates:
```qasm
qubit qb; qumode qm;

// displacement gate
disp(alpha) qm;
// SNAP gate
snap([theta0,...,thetak]) qm;
// ECD gate
ecd(alpha) qb, qm;
```
This necessitates the following changes:

## 1. Passing array as gate parameters
The SNAP gate can take an arbitrary number of parameters. To make this explicit,
we require that these parameters be passed in as an array:
```qasm
snap([theta0,...,thetak]) qm;
```
This is supported by the following addition to the grammar:
```bison
ArgList
  : ...
  | '(' '[' ExprList ']' ')' {
    $$ = ASTArgumentNodeBuilder::Instance().NewList();
    ASTAngleArrayNode *AAN =
        ASTProductionFactory::Instance().ProductionRule_10010($3);
    $$->Append(AAN);
  }
```
Currently, we only allow gates to either take a fixed number of parameters or
an array of parameters.

## 2. Passing qumodes as gate operands
The gate call syntax supports `qumode` operands as well as `qubit`
operands (and mixed operands, e.g. ECD). Currently, no type checking is done,
so both `ecd(alpha) qb, qm` and `ecd(alpha) qm, qb` are allowed. However,
only `ecd(alpha) qb, qm` is considered valid syntax.
Typed operand checking requires revising the gate definition
syntax (also see [Gate Declarations](#gate-declarations-low-priority)).

## 3. (WIP) Passing complex numbers as gate parameters
The displacement gate and ECD gate can take complex numbers as parameters:
```qasm
disp(0.3+0.5i) qm;
ecd(0.5-0.3i) qb, qm;

disp(0.3) qm; // equivalent to disp(0.3+0i) qm;
ecd(-0.5i) qb, qm; // equivalent to ecd(0-0.5i) qb, qm;
```



## 4. (WIP) Adding new builtin gates
**Current workaround:** include the `cvgates.inc` libraries that defines the new
gates as opaque gates.

Displacement gates (`disp`) should be a builtin gate. SNAP gates and ECD gates
can be defined in terms of control gate syntax. That said, we might want to
treat SNAP and ECD gates as builtin gates, until we have proper gate definition
syntaxes (also see [Gate Declarations](#gate-declarations-low-priority)).


# (WIP) Unitary Declarations

```qasm
unitary u = [
    [(1+i)/2, (1-i)/2, 0],
    [(1-i)/2, (1+i)/2, 0],
    [0, 0, 1],
];

qumode qm;
// unitary can be applied as a gate
u qm;
```


# (WIP) Fields (for bumper states and etc.)

Sometimes we want to attach additional information to a variable. For example,
suppose we want to attach the number of bumper states that is used when
decomposing the unitary into a sequence of gates:
```qasm
u.bumper = 4;
```


# Gate Declarations (Low-priority)

The addition of qumode gates may necessitate the parameters and operands to be
typed. gate declarations. For example, may be something like the following:
```qasm
gate snap<uint N>(array[phases, N] thetas) qumode qm {
    for i in [0:N] {
        ctrl(i) @ gphase(thetas[i]) qm;
    }
}

gate ecd(complex[float] alpha) qubit ctrl, qumode target {
    negctrl @ disp(-alpha/2) ctrl, target;
    ctrl @ disp(alpha/2) ctrl, target;
}
```
New components:
- types for parameters,
- types for gate operands,
- `ctrl` modifier for qumodes

Backward compatibility with OPENQASM 3.0 could be a problem here---we may still
want to be able to `include` OPENQASM 3.0 headers like `stdgates.inc`.
