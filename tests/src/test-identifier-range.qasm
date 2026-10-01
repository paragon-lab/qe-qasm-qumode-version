OPENQASM 3.0;

// Registers sized by an int, and for-ranges whose bounds are ints.
// [0:N] steps by 1; [0:2:N] steps by 2.
// Start and step may be names, and `N-1` is an end expression.

gate h q {
  U(pi/2, 0, pi) q;
}

int N = 3;
int STEP = 2;
int BEGIN = 0;

qubit[N] q;
qubit[N+1] qe;
qubit[(N+1)*2] qw;
qumode[N] qm;
qumode[N-1] qme;
bit[N] c;
bit[N+1] ce;
int[N+1] ni;
uint[N] ui;
float[(N+1)*16] fl;
angle[N] ang;
array[int[N+1], N] xs;

for i in [0:N] {
  h q[i];
}

for i in [0:2:N] {
  h q[i];
}

for i in [BEGIN:N-1] {
  h q[i];
}

for i in [BEGIN:STEP:N-1] {
  h q[i];
}

for i in {1, 2, (N+1)*2} {
}

for i in [0:2:N-1] {
  h q[i];
}

for i in [N-1:N] {
  h q[i];
}

for i in [0:N+1:N*2] {
  h q[i];
}

for i in [BEGIN+1:STEP:N+STEP] {
  h q[i];
}

for i in [0:N-1-2] {
  h q[i];
}
