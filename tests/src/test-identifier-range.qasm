OPENQASM 3.0;

// Registers sized by an int, and for-ranges whose end is that int.
// [0:N] steps by 1; [0:2:N] steps by 2.

gate h q {
  U(pi/2, 0, pi) q;
}

int N = 3;

qubit[N] q;
qumode[N] qm;
bit[N] c;

for i in [0:N] {
  h q[i];
}

for i in [0:2:N] {
  h q[i];
}
