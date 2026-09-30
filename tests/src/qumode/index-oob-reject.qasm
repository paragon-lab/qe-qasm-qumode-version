OPENQASM 3.0;

include "cvgates.inc";

qumode[2] qm;

// Index equal to the register size must be a diagnostic, not an assertion.
disp(0.3) qm[2];
