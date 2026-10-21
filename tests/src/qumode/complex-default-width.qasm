OPENQASM 3.0;

// `complex` and `complex[float]` infer complex[float[64]].
complex[float] f = 3.14 + 6.28im;
complex g = 1.0 + 2.0im;

gate foo(complex alpha) qumode qm {
  disp(alpha) qm;
}

gate bar(complex[float] beta) qumode qm {
  disp(beta) qm;
}

qumode qm;
foo(f) qm;
bar(g) qm;
foo(0.5 + 0.3im) qm;
bar(0.3) qm;
