// CV / qumode gate definitions.
// disp is a builtin gate with the following signature:
// gate disp(complex[float[64]] alpha) qumode qm;

// snap remains opaque until compile-time known length variable-length array
// gate parameters are supported.
opaque snap(thetas) qm;

gate ecd(complex[float[64]] alpha) qubit qb, qumode qm {
    negctrl @ disp(-alpha/2) qb, qm;
    ctrl @ disp(alpha/2) qb, qm;
}
