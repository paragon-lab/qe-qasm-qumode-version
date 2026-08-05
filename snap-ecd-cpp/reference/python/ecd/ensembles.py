"""Ideal circuit-ensemble samplers and second-moment estimators.

Statistics:
  collision(q)      = Σ_x q_x²   (Haar mean: 2/(D+1))
  hog(q)            = heavy-output probability, strict-median convention
  frame_potential_2 = E|tr(U†V)|⁴ over independent pairs (Haar value: 2)
"""

import itertools

import numpy as np

F2_HAAR = 2.0


def harmonic(n):
    return float(np.sum(1.0 / np.arange(1, n + 1)))


def hog_ideal(D):
    """Exact Haar-ensemble mean HOG: 0.5*(1 + H_D - H_{D/2})."""
    return 0.5 * (1 + harmonic(D) - harmonic(D // 2))


def collision_haar(D):
    return 2.0 / (D + 1)


def hog(q):
    return float(q[q > np.median(q)].sum())


def collision(q):
    return float(np.sum(np.asarray(q) ** 2))


def haar_state(dim, rng):
    g = rng.standard_normal(dim) + 1j * rng.standard_normal(dim)
    return g / np.linalg.norm(g)


def haar_unitary(dim, rng):
    g = rng.standard_normal((dim, dim)) + 1j * rng.standard_normal((dim, dim))
    Q, R = np.linalg.qr(g)
    return Q * (np.diag(R) / np.abs(np.diag(R)))


def _mode_permutation_matrix(perm, d):
    """Unitary permuting tensor factors of m d-level modes: factor i ← factor perm[i]."""
    m = len(perm)
    P = np.zeros((d ** m, d ** m))
    for idx in range(d ** m):
        digits = [(idx // d ** (m - 1 - i)) % d for i in range(m)]
        new_digits = [digits[perm[i]] for i in range(m)]
        new_idx = sum(dig * d ** (m - 1 - i) for i, dig in enumerate(new_digits))
        P[new_idx, idx] = 1.0
    return P


def qv_brickwork_unitary(m, depth, rng, d=2):
    """QV model circuit on m d-level modes, following the convention of
    Cross et al., PRA 100, 032328 (2019): per layer, a uniformly random mode
    permutation followed by Haar U(d²) on adjacent mode pairs (last mode idles
    when m is odd). d=2 is the standard qubit QV circuit."""
    D = d ** m
    U = np.eye(D, dtype=complex)
    for _ in range(depth):
        layer = _mode_permutation_matrix(rng.permutation(m), d)
        gates = [haar_unitary(d * d, rng) for _ in range(m // 2)]
        block = gates[0] if gates else np.eye(1)
        for g in gates[1:]:
            block = np.kron(block, g)
        if m % 2 == 1:
            block = np.kron(block, np.eye(d))
        U = block @ layer @ U
    return U


def frame_potential_2(unitary_sampler, n_samples, rng):
    """Monte-Carlo frame potential F^(2) = E|tr(U†V)|⁴ over all ordered pairs
    of n_samples independent draws"""
    Us = [unitary_sampler(rng) for _ in range(n_samples)]
    total, count = 0.0, 0
    for i, j in itertools.combinations(range(n_samples), 2):
        total += np.abs(np.trace(Us[i].conj().T @ Us[j])) ** 4
        count += 1
    return total / count
