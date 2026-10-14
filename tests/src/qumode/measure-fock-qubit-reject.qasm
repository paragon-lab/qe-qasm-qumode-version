OPENQASM 3.0;

qubit q;
bit c;
c = measure_photon_count(0) q;
