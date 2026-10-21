OPENQASM 3.0;

gate idq qumode q {
}

qumode qm;

// Negative: fully-typed gate with no classical formals rejects extra args.
idq(0.5) qm;
