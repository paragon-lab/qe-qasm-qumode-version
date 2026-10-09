OPENQASM 3.0;

// Pure-imaginary literals (spaced and glued), including unary minus.
complex[float[64]] a = 1.0 im;
complex[float[64]] b = 1.0im;
complex[float[64]] c = -1.0 im;
complex[float[64]] d = -1.0im;
complex[64] e = 2 im;
complex[64] f = -3im;

// Still accept the real + imag form.
complex[float[64]] g = 0.0 + -1.0im;
