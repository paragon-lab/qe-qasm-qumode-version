OPENQASM 3.0;

// Negative: a declared gate is not a qumode target.
gate qm qumode q {
}

disp(0.5) qm;
