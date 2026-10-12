OPENQASM 3.0;

qumode qm;
qumode[2] qms;
bit c;
bit[2] cs;
uint[32] n = 3;

measure(fock=0) qm;
c = measure(fock=1) qm;
c = measure(fock=n) qm;
c = measure(fock=1 + 2) qm;
measure(fock=2) qms[1];
cs[0] = measure(fock=2) qms[1];
