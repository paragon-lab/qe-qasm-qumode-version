OPENQASM 3.0;

// Builtin beamsplitter: no include required.
qumode qm1;
qumode qm2;
qumode[2] qms;

angle theta = pi / 4;
angle phi = 0;

bs(pi / 4, 0) qm1, qm2;
bs(theta, phi) qm1, qm2;
bs(pi / 2, pi / 3) qms[0], qms[1];
