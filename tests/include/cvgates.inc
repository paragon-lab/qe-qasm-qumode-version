// Opaque CV gates. Operands are untyped quantum params so call sites may
// pass qubits, qumodes, or a mix (e.g. ECD).
// disp is a builtin gate (complex param + qumode operand).

opaque snap(thetas) qm;

// Note: this definition is loose; it does not enforce that:
// - `alpha` is a complex number
// - `qb` is a qubit
// - `qm` is a qumode
// Proper type checking is yet to be implemented.

gate ecd(alpha) qb, qm {
    negctrl @ disp(alpha/2) qb, qm;
    ctrl @ disp(alpha/2) qb, qm;
}
