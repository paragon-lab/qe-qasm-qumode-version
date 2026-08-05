import numpy as np
import pytest

from ensembles import haar_state
from circuit_io import CompiledCircuit
from optimization.ecd_parameter_finder import (
    CompileJob,
    ECDParameterFinder,
    OptimizerConfig,
    compile_circuit_worker,
    run_circuit,
    run_unitary_circuit,
    stability_infidelity,
)


def test_run_circuit_identity_like():
    """Initial R(π) moves |g⟩→|e⟩; ECD(0) swaps blocks back, so the g-block
    holds vacuum with unit norm and no leakage."""
    betas     = np.zeros((1, 1), dtype=complex)
    rotations = np.array([[np.pi, 0.0], [0.0, 0.0]])
    psi_g, penalty, boundary = run_circuit(betas, rotations, N=4)
    psi_g = np.array(psi_g)
    assert np.abs(psi_g[0]) == pytest.approx(1.0, abs=1e-12)
    assert float(penalty) == pytest.approx(0.0, abs=1e-12)
    assert float(boundary) == pytest.approx(0.0, abs=1e-12)


def test_run_circuit_displacement_leaks():
    """ECD(β=2) sends the |e⟩ branch through D(−β/2)|0⟩; the top-level population
    must match an independent scipy expm of the truncated displacement."""
    from scipy.linalg import expm

    N         = 4
    betas     = np.array([[2.0 + 0j]])
    rotations = np.array([[np.pi, 0.0], [0.0, 0.0]])
    _, _, boundary = run_circuit(betas, rotations, N=N)

    a        = np.diag(np.sqrt(np.arange(1, N)), 1)
    D        = expm(-1.0 * a.conj().T + 1.0 * a)      # D(−β/2), β/2 = 1
    expected = np.abs(D[N - 1, 0]) ** 2
    assert float(boundary) == pytest.approx(expected, rel=1e-9)


def test_run_circuit_penalty_matches_boundary_weighting():
    """With n_penalize=1 the penalty weights the top level by e⁰=1 per ECD step,
    so for a single-step circuit penalty == boundary population."""
    betas     = np.array([[2.0 + 0j]])
    rotations = np.array([[np.pi, 0.0], [0.0, 0.0]])
    _, penalty, boundary = run_circuit(betas, rotations, N=4, n_penalize=1)
    assert float(penalty) == pytest.approx(float(boundary), rel=1e-9)


def test_zero_depth_unitary_circuit_returns_logical_identity():
    betas = np.zeros((0, 1), dtype=complex)
    rotations = np.zeros((1, 2))
    ground, penalty, boundary = run_unitary_circuit(
        betas, rotations, d=2, N=4)
    expected = np.zeros((4, 2), dtype=complex)
    expected[:2] = np.eye(2)
    np.testing.assert_allclose(ground, expected, atol=1e-12)
    assert float(penalty) == pytest.approx(0.0, abs=1e-12)
    assert float(boundary) == pytest.approx(0.0, abs=1e-12)


def test_lbfgs_compiles_complete_identity_unitary():
    config = OptimizerConfig(err_th=1e-8, lbfgs_starts=1, n_steps=200)
    finder = ECDParameterFinder(d=2, num_modes=1, config=config)
    circuit = finder.find_unitary_parameters(
        np.eye(2, dtype=complex), k=0, N=2,
        rng=np.random.default_rng(3))
    assert circuit is not None
    assert circuit.infidelity <= config.err_th
    assert circuit.target_unitary.shape == (2, 2)


class TestFindParameters:

    @pytest.fixture(scope="class")
    @classmethod
    def converged(cls):
        rng    = np.random.default_rng(7)
        target = haar_state(2, rng)
        config = OptimizerConfig(err_th=0.01, lbfgs_starts=8, n_steps=500)
        finder = ECDParameterFinder(d=2, num_modes=1, config=config)
        circuit = finder.find_parameters(target, k=4, N=6,
                                         rng=np.random.default_rng(3))
        return finder, target, circuit

    def test_converges_to_compiled_circuit(self, converged):
        _, target, circuit = converged
        assert isinstance(circuit, CompiledCircuit)
        assert circuit.infidelity <= 0.01
        assert circuit.depth == 4
        assert circuit.betas.shape == (4, 1)
        assert circuit.rotations.shape == (5, 2)
        np.testing.assert_allclose(circuit.target_probabilities,
                                   np.abs(target) ** 2)

    def test_opt_trace_recorded(self, converged):
        """L-BFGS trace stores the best infidelity after each restart."""
        _, _, circuit = converged
        assert circuit.optimization_trace is not None
        assert 1 <= len(circuit.optimization_trace) <= 8
        assert np.all(np.diff(circuit.optimization_trace) <= 1e-9)
        assert circuit.optimization_trace.min() <= circuit.infidelity + 1e-9

    def test_reported_err_matches_replay(self, converged):
        """err must equal 1−|⟨target|ψ_g⟩| when the stored parameters are replayed
        (penalty-free replay: finder was built with penalty_weight=0)."""
        finder, target, circuit = converged
        stab = stability_infidelity(circuit, target, [6], finder.d,
                                    finder.num_modes)
        assert stab[0] == pytest.approx(circuit.infidelity, abs=1e-9)

    def test_stability_infidelity_shape(self, converged):
        finder, target, circuit = converged
        stab = stability_infidelity(circuit, target, [6, 7, 8], finder.d,
                                    finder.num_modes)
        assert stab.shape == (3,)
        assert np.all((0.0 <= stab) & (stab <= 1.0))

    def test_stability_acceptance(self, converged):
        """With stability_th set, an accepted circuit must replay below the
        threshold at larger truncations (or the finder returns None)."""
        _, target, _ = converged
        config = OptimizerConfig(err_th=0.01, lbfgs_starts=8, n_steps=500,
                                 stability_th=1e-2,
                                 n_test_extra=(1, 2, 3))
        finder = ECDParameterFinder(d=2, num_modes=1, config=config)
        circuit = finder.find_parameters(target, k=4, N=6,
                                         rng=np.random.default_rng(3))
        if circuit is not None:
            stab = stability_infidelity(circuit, target, [7, 8, 9], 2, 1)
            assert float(np.nanmax(stab)) < 1e-2

    def test_returns_none_when_unreachable(self):
        rng    = np.random.default_rng(11)
        target = haar_state(4, rng)
        config = OptimizerConfig(err_th=1e-6, lbfgs_starts=2, n_steps=100)
        finder = ECDParameterFinder(d=4, num_modes=1, config=config)
        assert finder.find_parameters(target, k=1, N=8,
                                      rng=np.random.default_rng(0)) is None

    def test_adaptive_k_records_sweep(self):
        """Bottom-up sweep from k=1 must record (k, best infidelity) for every
        depth tried, ending at the accepted minimum depth."""
        rng    = np.random.default_rng(7)
        target = haar_state(2, rng)
        config = OptimizerConfig(err_th=0.01, lbfgs_starts=8, n_steps=500)
        finder = ECDParameterFinder(d=2, num_modes=1, config=config)
        circuit = finder.find_parameters_adaptive_k(
            target, k_init=1, k_max=4, N=6, rng=np.random.default_rng(3))
        assert circuit is not None
        assert circuit.depth_sweep is not None
        ks = circuit.depth_sweep[:, 0]
        np.testing.assert_array_equal(ks, np.arange(1, len(ks) + 1))
        assert circuit.depth == int(ks[-1])
        assert circuit.depth_sweep[-1, 1] <= 0.01
        assert np.all(circuit.depth_sweep[:-1, 1] > 0.01) or len(ks) == 1


def test_worker_matches_direct_call():
    rng    = np.random.default_rng(5)
    target = haar_state(2, rng)
    seed   = 123

    config = OptimizerConfig(err_th=0.01, lbfgs_starts=8, n_steps=500)
    job = CompileJob(d=2, num_modes=1, config=config, target=target,
                     k_init=4, k_max=4, k_step=1, N=6, seed=seed)
    from_worker = compile_circuit_worker(job)

    finder = ECDParameterFinder(d=2, num_modes=1, config=config)
    direct = finder.find_parameters_adaptive_k(
        target, k_init=4, k_max=4, N=6, rng=np.random.default_rng(seed))

    assert (from_worker is None) == (direct is None)
    assert from_worker is not None, "expected an easy target to converge"
    np.testing.assert_allclose(from_worker.betas, direct.betas)
    np.testing.assert_allclose(from_worker.rotations, direct.rotations)
    assert from_worker.infidelity == pytest.approx(direct.infidelity)
