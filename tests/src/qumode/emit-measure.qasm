OPENQASM 3.0;

qubit q;
qubit[2] qs;
qumode qm;
qumode[2] qms;
bit b;
bit[2] bs;

measure q;
b = measure q;
measure qs[1];
bs[0] = measure qs[1];
measure(fock=1) qm;
b = measure(fock=1) qm;
measure(fock=2) qms[1];
bs[0] = measure(fock=2) qms[1];
