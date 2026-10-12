OPENQASM 3.0;

bit[32] acc;

for i in [0 : 4] {
  acc |= i;
}
