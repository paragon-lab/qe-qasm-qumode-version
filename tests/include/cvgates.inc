// Opaque CV gates. Operands are untyped quantum params so call sites may
// pass qubits, qumodes, or a mix (e.g. ECD).
opaque snap(thetas) qm;
opaque ecd(alpha) qb, qm;
opaque disp(alpha) qm;
