OPENQASM 3.0;

include "cvgates.inc";

gate myu(theta, phi, lambda) q {
  U(theta, phi, lambda) q;
}

gate mydisp(complex[float[64]] alpha) qumode qm {
  disp(alpha) qm;
}

gate mydisp2(complex alpha) qumode qm {
  disp(alpha) qm;
}

gate mydisp3(complex[float] beta) qumode qm {
  disp(beta) qm;
}

gate foo[uint N](array[float[64], N] thetas) qubit q {
}

gate bar(angle alpha) qubit q {
  ctrl @ gphase(alpha) q;
}

gate idq qumode q {
}

qubit q;
qumode qm;
myu(pi / 2, 0, pi) q;
mydisp(0.5 + 0.3im) qm;
bar(pi / 4) q;
