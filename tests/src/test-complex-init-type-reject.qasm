OPENQASM 3.0;

angle[32] a = pi;
bit b;

// Negative: identifier R/I parts must be numeric scalars, not angle/bit.
complex[float[64]] c = a + b im;
