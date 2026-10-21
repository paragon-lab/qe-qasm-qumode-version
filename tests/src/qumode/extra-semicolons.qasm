OPENQASM 3.0;

// Redundant semicolons after `}` and as empty statements are ignored.
gate idq qumode q {
};

gate idq2 qumode q {
}

;
qumode qm;;
idq qm;
idq2 qm;
