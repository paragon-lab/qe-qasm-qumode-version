OPENQASM 3.0;

// Fully-typed gates may omit classical parameters (no-parens or empty parens).
gate idq qumode q {
}

gate idq_parens() qumode q {
}

gate idqubit qubit q {
}

gate fock_id[uint N]() qumode q {
  ctrl[N] @ gphase(0) q;
}

gate fock_id_np[uint N] qumode q {
  ctrl[N] @ gphase(0) q;
}

qumode qm;
qubit qb;

idq qm;
idq_parens() qm;
idqubit qb;
fock_id[3] qm;
fock_id_np[3] qm;
