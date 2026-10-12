OPENQASM 3.0;

bit[32] acc;

int x = 3;

if (x < 4) {
  for i in [0 : 4] {
    acc |= i;
  }
}
