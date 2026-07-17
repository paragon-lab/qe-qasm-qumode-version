OPENQASM 3.0;

include "cvgates.inc";

qubit qb;
qumode qm;

gate rz(theta) q {
    ctrl @ gphase(theta) q;
}

rz(pi/2) qb;
ecd(0.5) qb, qm;
snap([pi/2, 0, pi/3]) qm;
disp(0.3) qm;
