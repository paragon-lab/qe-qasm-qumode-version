OPENQASM 3.0;

include "cvgates.inc";

qumode[4] qm;

int k = 1;
disp(0) qm[k];

for int i in [0:4] {
  disp(0) qm[i];
}
