"""
Parameter optimization at the gate level.

All kets are laid out as transmon ⊗ cavity_0 ⊗ … ⊗ cavity_{m-1}:

    psi : (2 * N^m,) complex vector
          psi[:N^m]  — excited-state (|e⟩) block
          psi[N^m:]  — ground-state  (|g⟩) block
"""
from __future__ import annotations

import itertools
import os
from dataclasses import dataclass
from functools import partial

import numpy as np

import jax
jax.config.update("jax_enable_x64", True)
import jax.numpy as jnp

import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from gates import _ladder_ops, _embed_cavity, _ecd, _rotation_matrix
from circuit_io import CompiledCircuit



def _mode_fock_probs(psi, num_modes: int, N: int) -> jax.Array:
    """Per-mode Fock populations, shape (num_modes, N), traced over the transmon
    and the other modes."""
    dim_cav = N ** num_modes
    joint   = (jnp.abs(psi[:dim_cav]) ** 2 + jnp.abs(psi[dim_cav:]) ** 2).reshape([N] * num_modes)
    return jnp.stack([
        joint.sum(axis=tuple(i for i in range(num_modes) if i != j))
        for j in range(num_modes)
    ])


def _evolve(betas, rotations, initial_ground, N: int, n_penalize: int):
    """Shared metriq-qudits evolution extended to one or more input columns."""
    k, num_modes = betas.shape
    a, adag = _ladder_ops(N)
    weights = jnp.exp(jnp.arange(N - n_penalize, N) + 1.0 - N)

    excited = jnp.zeros_like(initial_ground)
    ground = initial_ground

    def apply_rotation(excited, ground, rotation):
        matrix = _rotation_matrix(rotation[0], rotation[1])
        return (matrix[0, 0] * excited + matrix[0, 1] * ground,
                matrix[1, 0] * excited + matrix[1, 1] * ground)

    excited, ground = apply_rotation(excited, ground, rotations[0])
    layer_rotations = rotations[1:].reshape(k, num_modes, 2)

    def layer_step(carry, layer_inputs):
        excited, ground, penalty, boundary = carry
        betas_l, rotations_l = layer_inputs
        for j in range(num_modes):
            D_neg, D_pos = _ecd(betas_l[j], a, adag)
            D_neg_full = _embed_cavity(D_neg, j, num_modes, N)
            D_pos_full = _embed_cavity(D_pos, j, num_modes, N)
            excited, ground = D_pos_full @ ground, D_neg_full @ excited

            def column_probabilities(excited_column, ground_column):
                return _mode_fock_probs(
                    jnp.concatenate([excited_column, ground_column]),
                    num_modes, N)

            probabilities = jax.vmap(
                column_probabilities, in_axes=(1, 1))(excited, ground)
            boundary = jnp.maximum(boundary,
                                   probabilities[:, :, -1].max())
            penalty = penalty + (
                probabilities[:, :, N - n_penalize:] * weights
            ).sum() / initial_ground.shape[1]
            excited, ground = apply_rotation(
                excited, ground, rotations_l[j])
        return (excited, ground, penalty, boundary), None

    initial = (excited, ground, jnp.zeros((), dtype=jnp.float64),
               jnp.zeros((), dtype=jnp.float64))
    (excited, ground, penalty, boundary), _ = jax.lax.scan(
        layer_step, initial, (betas, layer_rotations))
    return ground, penalty, boundary


@partial(jax.jit, static_argnames=("N", "n_penalize"))
def run_circuit(betas, rotations, N: int, n_penalize: int = 0):
    """Run the ECD+rotation circuit from |g⟩⊗|vac⟩.

    betas     : complex (k, num_modes) ECD displacement amplitudes
    rotations : (k*num_modes + 1, 2) [theta, phi] per rotation

    Returns (psi_g, penalty, max_boundary_pop):
      psi_g            — ground-state cavity block of the final state
      penalty          — accumulated exp-weighted population of the top
                         n_penalize Fock levels after each ECD
      max_boundary_pop — max top-Fock-level population reached after any ECD
                         (the boundary-leakage diagnostic)

    The k layers are executed with jax.lax.scan (rather than a Python loop) so
    the compiled graph is one layer body regardless of depth. The num_modes
    inner loop stays unrolled.
    """
    num_modes = betas.shape[1]
    vacuum = jnp.zeros((N ** num_modes, 1), dtype=jnp.complex128)
    vacuum = vacuum.at[0, 0].set(1.0)
    ground, penalty, boundary = _evolve(
        betas, rotations, vacuum, N, n_penalize)
    return ground[:, 0], penalty, boundary


def logical_embedding(d: int, num_modes: int, N: int) -> jax.Array:
    """Embed the d**num_modes logical basis into the N-level Fock basis."""
    embedding = np.zeros((N ** num_modes, d ** num_modes), dtype=complex)
    for logical_index, levels in enumerate(
            itertools.product(range(d), repeat=num_modes)):
        fock_index = sum(level * N ** (num_modes - mode - 1)
                         for mode, level in enumerate(levels))
        embedding[fock_index, logical_index] = 1.0
    return jnp.asarray(embedding)


@partial(jax.jit, static_argnames=("d", "N", "n_penalize"))
def run_unitary_circuit(betas, rotations, d: int, N: int,
                        n_penalize: int = 0):
    """Run every embedded logical basis input through one ECD circuit."""
    embedding = logical_embedding(d, betas.shape[1], N)
    return _evolve(betas, rotations, embedding, N, n_penalize)


def _unpack(params, k: int, num_modes: int):
    n_beta    = 2 * k * num_modes
    b         = params[:n_beta].reshape(k, num_modes, 2)
    betas     = b[..., 0] + 1j * b[..., 1]
    rotations = params[n_beta:].reshape(k * num_modes + 1, 2)
    return betas, rotations


LOG_EPS = 1e-12


def _objective(p, psi_target, penalty_weight, k, num_modes, N, n_penalize):
    """log(composite loss): a monotone transform of the original
    infidelity + penalty objective that keeps gradients alive near convergence."""
    betas, rotations = _unpack(p, k, num_modes)
    psi_g, penalty, _ = run_circuit(betas, rotations, N, n_penalize)
    overlap_loss = 1.0 - jnp.abs(jnp.dot(psi_target.conj(), psi_g))
    return jnp.log(overlap_loss + penalty_weight * penalty + LOG_EPS)


# Scalar value-and-gradient of the objective, used by the L-BFGS optimizer. The
# scipy driver calls this many times per start, so a single jitted graph (compiled
# once per (k, N)) is reused across every start and iteration.
_value_and_grad = jax.jit(jax.value_and_grad(_objective), static_argnums=(3, 4, 5, 6))


def _unitary_infidelity(unitary_target, ground_block, d, num_modes, N):
    target = logical_embedding(d, num_modes, N) @ unitary_target
    overlap = jnp.vdot(target, ground_block) / unitary_target.shape[0]
    return 1.0 - jnp.abs(overlap)


def _unitary_objective(p, unitary_target, penalty_weight, d, k, num_modes,
                       N, n_penalize):
    betas, rotations = _unpack(p, k, num_modes)
    ground, penalty, _ = run_unitary_circuit(
        betas, rotations, d, N, n_penalize)
    overlap_loss = _unitary_infidelity(
        unitary_target, ground, d, num_modes, N)
    return jnp.log(overlap_loss + penalty_weight * penalty + LOG_EPS)


_unitary_value_and_grad = jax.jit(
    jax.value_and_grad(_unitary_objective),
    static_argnums=(3, 4, 5, 6, 7),
)


def _pad(psi_haar, M: int, d: int, num_modes: int) -> np.ndarray:
    padded = np.zeros(M ** num_modes, dtype=complex)
    for idx, xs in enumerate(itertools.product(range(d), repeat=num_modes)):
        cav_idx = sum(x * M ** (num_modes - 1 - i) for i, x in enumerate(xs))
        padded[cav_idx] = psi_haar[idx]
    return padded / np.linalg.norm(padded)


@dataclass(frozen=True)
class OptimizerConfig:
    """Search hyperparameters for the ECD parameter optimizer.

    stability_th: when set, a converged candidate is accepted only if its max
    stability infidelity (replay at N + n_test_extra) stays below it. Setting it
    to None returns the best converged candidate regardless (used by buffer calibration,
    which measures stability curves itself)."""

    n_penalize: int = 0
    penalty_weight: float = 0.0
    err_th: float = 0.01
    lbfgs_starts: int = 64        # max L-BFGS restarts; stops at the first accepted candidate
    n_steps: int = 2000
    stability_th: float | None = None
    n_test_extra: tuple[int, ...] = tuple(range(1, 13))


@dataclass(frozen=True, eq=False)
class CompileJob:
    """A single picklable compile task for compile_circuit_worker."""

    d: int
    num_modes: int
    config: OptimizerConfig
    target: np.ndarray
    k_init: int
    k_max: int
    k_step: int
    N: int
    seed: int


@dataclass(frozen=True)
class CompiledUnitary:
    """Reference result for complete logical-unitary ECD compilation."""

    betas: np.ndarray
    rotations: np.ndarray
    target_unitary: np.ndarray
    infidelity: float
    boundary_leakage: float
    optimization_trace: np.ndarray

    @property
    def depth(self) -> int:
        return self.betas.shape[0]


class ECDParameterFinder:
    def __init__(self, d: int, num_modes: int, config: OptimizerConfig):
        self.d         = d
        self.num_modes = num_modes
        self.cfg       = config

    def _make_circuit(self, params, target_haar, k: int, N: int,
                      err: float, opt_trace, k_sweep=None) -> CompiledCircuit:
        betas, rotations = _unpack(np.asarray(params), k, self.num_modes)
        betas, rotations = np.array(betas), np.array(rotations)
        _, _, boundary   = run_circuit(betas, rotations, N)
        return CompiledCircuit(
            betas=betas,
            rotations=rotations,
            target_state=np.asarray(target_haar, dtype=complex),
            boundary_leakage=float(boundary),
            infidelity=float(err),
            optimization_trace=np.array(opt_trace),
            depth_sweep=k_sweep,
        )

    def _search_at_k_lbfgs(self, target_haar, k: int, N: int, rng, verbose: bool):
        """Restart-until-accepted scipy L-BFGS-B at fixed depth k: run
        L-BFGS from a random start and accept the first candidate that converges
        below err_th (and, when stability_th is set, passes the stability test),
        restarting up to lbfgs_starts times."""
        from scipy.optimize import minimize

        cfg        = self.cfg
        psi_target = jnp.array(_pad(target_haar, N, self.d, self.num_modes))
        m          = self.num_modes
        n_beta     = 2 * k * m
        n_rot      = 2 * (k * m + 1)

        def fun(x):
            val, grad = _value_and_grad(
                jnp.asarray(x), psi_target, cfg.penalty_weight, k, m, N, cfg.n_penalize)
            return float(val), np.asarray(grad, dtype=np.float64)

        def infidelity(x):
            betas, rotations = _unpack(jnp.asarray(x), k, m)
            psi_g, _, _ = run_circuit(betas, rotations, N, cfg.n_penalize)
            return 1.0 - float(jnp.abs(jnp.dot(psi_target.conj(), psi_g)))

        best_infid, guess, opt_trace = np.inf, None, []
        for s in range(cfg.lbfgs_starts):
            cold = np.concatenate([
                rng.standard_normal(n_beta),
                rng.uniform(0, 2 * np.pi, n_rot),
            ])
            x0  = cold if guess is None else (guess + cold) / 2
            res = minimize(fun, x0, jac=True, method="L-BFGS-B",
                           options={"maxiter": cfg.n_steps})
            guess = res.x
            infid = infidelity(res.x)
            best_infid = min(best_infid, infid)
            opt_trace.append(best_infid)
            if verbose:
                print(f"      k={k}  lbfgs restart {s + 1:3d}/{cfg.lbfgs_starts}"
                      f"  err={infid:.2e}  best={best_infid:.2e}", flush=True)

            if infid <= cfg.err_th:
                if cfg.stability_th is None:
                    return res.x, best_infid, infid, opt_trace
                circuit = self._make_circuit(res.x, target_haar, k, N, infid, opt_trace)
                stab = stability_infidelity(
                    circuit, target_haar, [N + e for e in cfg.n_test_extra],
                    self.d, self.num_modes)
                if float(np.nanmax(stab)) < cfg.stability_th:
                    return res.x, best_infid, infid, opt_trace
                if verbose:
                    print(f"      candidate err={infid:.2e} UNSTABLE"
                          f" (max stab {float(np.nanmax(stab)):.2e})", flush=True)

        return None, best_infid, None, opt_trace

    def _search_unitary_at_k_lbfgs(self, unitary_target, k: int, N: int,
                                    rng, verbose: bool):
        """The metriq-qudits L-BFGS search applied to a complete unitary."""
        from scipy.optimize import minimize

        cfg = self.cfg
        target = jnp.asarray(unitary_target)
        logical_dimension = self.d ** self.num_modes
        if target.shape != (logical_dimension, logical_dimension):
            raise ValueError("unitary target has incompatible dimensions")
        if not np.allclose(np.asarray(target.conj().T @ target),
                           np.eye(logical_dimension), atol=1e-9):
            raise ValueError("unitary target is not unitary")

        m = self.num_modes
        n_beta = 2 * k * m
        n_rot = 2 * (k * m + 1)

        def fun(x):
            value, gradient = _unitary_value_and_grad(
                jnp.asarray(x), target, cfg.penalty_weight, self.d, k, m, N,
                cfg.n_penalize)
            return float(value), np.asarray(gradient, dtype=np.float64)

        def infidelity(x):
            betas, rotations = _unpack(jnp.asarray(x), k, m)
            ground, _, _ = run_unitary_circuit(
                betas, rotations, self.d, N, cfg.n_penalize)
            return float(_unitary_infidelity(target, ground, self.d, m, N))

        best_infidelity, guess, trace = np.inf, None, []
        for restart in range(cfg.lbfgs_starts):
            cold = np.concatenate([
                rng.standard_normal(n_beta),
                rng.uniform(0, 2 * np.pi, n_rot),
            ])
            initial = cold if guess is None else (guess + cold) / 2
            result = minimize(fun, initial, jac=True, method="L-BFGS-B",
                              options={"maxiter": cfg.n_steps})
            guess = result.x
            error = infidelity(result.x)
            best_infidelity = min(best_infidelity, error)
            trace.append(best_infidelity)
            if verbose:
                print(f"      k={k}  unitary lbfgs restart "
                      f"{restart + 1:3d}/{cfg.lbfgs_starts}  "
                      f"err={error:.2e}  best={best_infidelity:.2e}",
                      flush=True)
            if error <= cfg.err_th:
                return result.x, error, trace
        return None, None, trace

    def find_parameters(self, target_haar, k: int, N: int,
                        rng=None, verbose: bool = False) -> CompiledCircuit | None:
        """Optimize k-layer ECD parameters preparing target_haar in an N-level
        space at a single fixed depth."""
        rng = rng if rng is not None else np.random.default_rng()
        params, _, infidelity, trace = self._search_at_k_lbfgs(
            target_haar, k, N, rng, verbose)
        if params is None:
            return None
        return self._make_circuit(
            params, target_haar, k, N, infidelity, trace)

    def find_unitary_parameters(self, unitary_target, k: int, N: int,
                                rng=None,
                                verbose: bool = False) -> CompiledUnitary | None:
        """Compile a complete logical unitary; state preparation stays separate."""
        rng = rng if rng is not None else np.random.default_rng()
        params, error, trace = self._search_unitary_at_k_lbfgs(
            unitary_target, k, N, rng, verbose)
        if params is None:
            return None
        betas, rotations = _unpack(np.asarray(params), k, self.num_modes)
        _, _, boundary = run_unitary_circuit(
            betas, rotations, self.d, N, self.cfg.n_penalize)
        return CompiledUnitary(
            betas=np.asarray(betas),
            rotations=np.asarray(rotations),
            target_unitary=np.asarray(unitary_target, dtype=complex),
            infidelity=float(error),
            boundary_leakage=float(boundary),
            optimization_trace=np.asarray(trace),
        )

    def find_parameters_adaptive_k(self, target_haar, k_init: int, k_max: int, N: int,
                                   k_step: int = 1, rng=None,
                                   verbose: bool = False) -> CompiledCircuit | None:
        """Bottom-up minimum-depth sweep (Eickbusch et al. 2022, Fig. 2a): try
        k = k_init, k_init + k_step, …, recording the best infidelity at every
        depth. The returned circuit is compiled at the first depth with a
        stable, converged candidate and carries the whole sweep in k_sweep."""
        rng     = rng if rng is not None else np.random.default_rng()
        k_sweep = []
        for k in range(k_init, k_max + 1, k_step):
            params, best_infidelity, infidelity, trace = self._search_at_k_lbfgs(
                target_haar, k, N, rng, verbose)
            k_sweep.append([float(k), float(best_infidelity)])
            circuit = None if params is None else self._make_circuit(
                params, target_haar, k, N, infidelity, trace,
                np.array(k_sweep))
            if circuit is not None:
                return circuit
        return None

def stability_infidelity(circuit: CompiledCircuit, target_haar, N_test_values,
                         d: int, num_modes: int) -> np.ndarray:
    """Replay the circuit at larger truncations to detect dependence on the
    Fock-space boundary."""
    out = []
    for N_test in N_test_values:
        target      = _pad(target_haar, int(N_test), d, num_modes)
        psi_g, _, _ = run_circuit(circuit.betas, circuit.rotations, int(N_test))
        out.append(1.0 - float(np.abs(np.dot(target.conj(), np.array(psi_g)))))
    return np.array(out)


def compile_circuit_worker(job: CompileJob) -> CompiledCircuit | None:
    """Compile one target in a fresh process (picklable top-level function)."""
    finder = ECDParameterFinder(job.d, job.num_modes, job.config)
    return finder.find_parameters_adaptive_k(
        job.target, k_init=job.k_init, k_max=job.k_max, N=job.N,
        k_step=job.k_step, rng=np.random.default_rng(job.seed),
    )
