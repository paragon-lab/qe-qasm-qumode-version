OPENQASM 3.0;

// Two initialized unitaries must each keep their own 2x2 matrix.
// Regression: InitializerListImpl reused one builder list → concatenated 4x2.
unitary a = {
  {1, 0},
  {0, 1}
};

unitary b = {
  {0, 1},
  {1, 0}
};
