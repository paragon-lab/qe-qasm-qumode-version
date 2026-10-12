OPENQASM 3.0;

int acc = 0;

int i = 10;

int j = 20;

do {
  acc |= i--;

  while (j != 0) {
    acc = acc + 1;
    --j;
  }

} while (i > 3);
