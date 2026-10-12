OPENQASM 3.0;

int acc = 0;

int i = 10;

while (i > 3) {
  acc |= i--;
}
