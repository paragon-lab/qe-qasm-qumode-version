OPENQASM 3.0;

qumode qm;
qumode[2] qms;
bit c;
bit[2] cs;
uint[32] n = 3;

measure_photon_count(0) qm;
c = measure_photon_count(1) qm;
c = measure_photon_count(n) qm;
c = measure_photon_count(1 + 2) qm;
measure_photon_count(2) qms[1];
cs[0] = measure_photon_count(2) qms[1];
