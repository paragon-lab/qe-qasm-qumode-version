OPENQASM 3.0;

include "cvgates.inc";

qubit q;

snap([pi/2, 0, pi/3]) q;
snap([pi/2, 0, pi/3, pi/4]) q;
snap([pi/2]) q;
