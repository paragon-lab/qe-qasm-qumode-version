#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "ecd_parameter_finder.hpp"

namespace {

ecd::ECDOptions integration_options() {
    ecd::ECDOptions options;
    options.err_th = 5e-4;
    options.n_restarts = 5;
    options.lbfgs_max_iter = 3000;
    options.grad_method = ecd::GradMethod::FiniteDifference;
    options.fd_step = 1e-6;
    options.n_penalize = 2;
    options.penalty_weight = 0.1;
    return options;
}

double state_logical_leakage(const ecd::Vector& state, int logical_dim) {
    return state.tail(state.size() - logical_dim).squaredNorm();
}

double unitary_logical_leakage(const ecd::Matrix& ground_block,
                               int logical_dim) {
    if (ground_block.rows() == logical_dim) return 0.0;
    return ground_block.bottomRows(ground_block.rows() - logical_dim)
               .squaredNorm() /
           static_cast<double>(ground_block.cols());
}

ecd::Complex phase_aligning(const ecd::Matrix& target,
                            const ecd::Matrix& actual) {
    const ecd::Complex overlap =
        target.conjugate().cwiseProduct(actual).sum();
    return std::abs(overlap) == 0.0
               ? ecd::Complex(1.0, 0.0)
               : std::conj(overlap) / std::abs(overlap);
}

}

TEST_CASE("finite-difference ECD finder prepares a nontrivial state end to end",
          "[integration][ecd]") {
    constexpr int d = 2;
    constexpr int N = 4;
    constexpr int k = 4;
    const double inv_sqrt_two = 1.0 / std::sqrt(2.0);
    ecd::Vector target(2);
    target << inv_sqrt_two, ecd::Complex(0.0, inv_sqrt_two);

    const ecd::ECDOptions options = integration_options();
    const ecd::ECDParameterFinder finder(d, 1, options);
    const auto circuit = finder.find_state_parameters(target, k, N, 1701);

    REQUIRE(circuit.has_value());
    const ecd::Vector embedded_target = finder.pad_state(target, N);
    const ecd::CircuitResult replay =
        ecd::run_circuit(circuit->betas, circuit->rotations, N);
    const double replay_error =
        1.0 - std::abs(embedded_target.dot(replay.psi_g));
    const ecd::Complex state_alignment =
        std::conj(embedded_target.dot(replay.psi_g)) /
        std::abs(embedded_target.dot(replay.psi_g));
    const double phase_aligned_state_error =
        (state_alignment * replay.psi_g - embedded_target).norm();
    const double ground_return = replay.psi_g.squaredNorm();
    const double logical_leakage = state_logical_leakage(replay.psi_g, d);

    CAPTURE(circuit->err, replay_error, phase_aligned_state_error,
            ground_return, logical_leakage, replay.boundary);
    CHECK(circuit->err <= options.err_th);
    CHECK(std::abs(circuit->err - replay_error) < 1e-12);
    CHECK(replay_error < 5e-4);
    CHECK(phase_aligned_state_error < 0.04);
    CHECK(ground_return > 0.999);
    CHECK(logical_leakage < 5e-4);
    CHECK(replay.boundary < 0.01);
    CHECK(std::abs(circuit->boundary_leakage - replay.boundary) < 1e-12);

    constexpr int larger_N = N + 2;
    const ecd::Vector larger_target = finder.pad_state(target, larger_N);
    const ecd::CircuitResult larger_replay =
        ecd::run_circuit(circuit->betas, circuit->rotations, larger_N);
    const double larger_error =
        1.0 - std::abs(larger_target.dot(larger_replay.psi_g));
    const double larger_logical_leakage =
        state_logical_leakage(larger_replay.psi_g, d);

    CAPTURE(larger_error, larger_logical_leakage, larger_replay.boundary);
    CHECK(larger_error < 0.005);
    CHECK(larger_replay.psi_g.squaredNorm() > 0.995);
    CHECK(larger_logical_leakage < 0.004);
}

TEST_CASE("finite-difference ECD finder synthesizes Pauli X end to end",
          "[integration][ecd]") {
    constexpr int d = 2;
    constexpr int N = 4;
    constexpr int k = 8;
    ecd::Matrix target(2, 2);
    target << 0.0, 1.0,
              1.0, 0.0;

    const ecd::ECDOptions options = integration_options();
    const ecd::ECDParameterFinder finder(d, 1, options);
    const auto circuit = finder.find_unitary_parameters(target, k, N, 2903);

    REQUIRE(circuit.has_value());
    const ecd::UnitaryCircuitResult replay =
        ecd::run_unitary_circuit(circuit->betas, circuit->rotations, d, N);
    const ecd::Matrix embedding = ecd::logical_embedding(d, 1, N);
    const ecd::Matrix embedded_target = embedding * target;
    const double replay_error =
        1.0 - std::abs(embedded_target.conjugate()
                           .cwiseProduct(replay.ground_block)
                           .sum() /
                       static_cast<double>(d));
    const ecd::Matrix logical_operator =
        embedding.adjoint() * replay.ground_block;
    const ecd::Complex alignment = phase_aligning(target, logical_operator);
    const double phase_aligned_frobenius =
        (alignment * logical_operator - target).norm();
    const double logical_leakage =
        unitary_logical_leakage(replay.ground_block, d);
    const ecd::Matrix logical_identity = ecd::Matrix::Identity(d, d);
    const double isometry_error =
        (logical_operator.adjoint() * logical_operator - logical_identity)
            .norm();
    const double worst_ground_return = std::min(
        replay.ground_block.col(0).squaredNorm(),
        replay.ground_block.col(1).squaredNorm());

    CAPTURE(circuit->err, replay_error, phase_aligned_frobenius,
            logical_leakage, isometry_error, worst_ground_return,
            replay.boundary);
    CHECK(circuit->err <= options.err_th);
    CHECK(std::abs(circuit->err - replay_error) < 1e-12);
    CHECK(replay_error < 5e-4);
    CHECK(phase_aligned_frobenius < 0.04);
    CHECK(worst_ground_return > 0.998);
    CHECK(logical_leakage < 1e-3);
    CHECK(isometry_error < 0.004);
    CHECK(replay.boundary < 0.005);
    CHECK(std::abs(circuit->boundary_leakage - replay.boundary) < 1e-12);

    constexpr int larger_N = N + 2;
    const ecd::UnitaryCircuitResult larger_replay =
        ecd::run_unitary_circuit(circuit->betas, circuit->rotations, d,
                                 larger_N);
    const ecd::Matrix larger_embedding =
        ecd::logical_embedding(d, 1, larger_N);
    const ecd::Matrix larger_target = larger_embedding * target;
    const double larger_error =
        1.0 - std::abs(larger_target.conjugate()
                           .cwiseProduct(larger_replay.ground_block)
                           .sum() /
                       static_cast<double>(d));
    const double larger_leakage =
        unitary_logical_leakage(larger_replay.ground_block, d);

    CAPTURE(larger_error, larger_leakage, larger_replay.boundary);
    CHECK(larger_error < 0.006);
    CHECK(larger_leakage < 0.004);
}
