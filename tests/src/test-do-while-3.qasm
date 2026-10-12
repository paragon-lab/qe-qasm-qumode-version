OPENQASM 3.0;

int acc = 0;

int i = 10;

do {
  acc |= i--;
} while (i > 3);
