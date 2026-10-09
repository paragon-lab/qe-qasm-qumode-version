OPENQASM 3.0;

include "cvgates.inc";

// Real arithmetic in a complex array literal is a real part (imag 0),
// not a real/imag pair. `pi/3` must not be read as R=pi, I=3.
gate mixc(array[complex[float[64]], 3] alphas) qumode qm {
  disp(alphas[0]) qm;
  disp(alphas[1]) qm;
  disp(alphas[2]) qm;
}

qumode qm;
mixc([pi/3, 0.0 + 1.0im, pi/2]) qm;
