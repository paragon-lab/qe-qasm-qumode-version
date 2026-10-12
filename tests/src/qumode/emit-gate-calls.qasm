OPENQASM 3.0;

include "cvgates.inc";

qumode qm;
qumode[5] qms;

qubit q;
qubit[3] qs;

disp(0.5 + 0.3im) qm;
disp(0.3 - 0.5im) qm;
disp(1im) qms[3];
snap([pi / 2, 0, 1.57, pi, pi / 4]) qms;
ecd(0.3 - 0.5im) q, qm;
U(pi / 2, pi / 3, pi / 4) q;
U(pi / 2, pi / 3, pi / 4) qs[2];
