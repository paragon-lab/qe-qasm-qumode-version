#ifdef ECD_WITH_TORCH

#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ecd_parameter_finder.hpp"

namespace {

ecd::ECDOptions optimizer_options() {
    ecd::ECDOptions options;
    options.err_th = 5e-4;
    options.n_restarts = 5;
    options.lbfgs_max_iter = 3000;
    options.n_penalize = 2;
    options.penalty_weight = 0.1;
    options.grad_method = ecd::GradMethod::Autodiff;
    return options;
}

double state_logical_leakage(const ecd::Vector& ground,
                             const ecd::Matrix& embedding) {
    const ecd::Vector logical_projection =
        embedding * (embedding.adjoint() * ground);
    return (ground - logical_projection).squaredNorm();
}

double unitary_logical_leakage(const ecd::Matrix& ground_block,
                               const ecd::Matrix& embedding) {
    const ecd::Matrix logical_projection =
        embedding * (embedding.adjoint() * ground_block);
    return (ground_block - logical_projection).squaredNorm() /
           static_cast<double>(ground_block.cols());
}

double phase_aligned_state_error(const ecd::Vector& target,
                                 const ecd::Vector& actual) {
    const ecd::Complex overlap = target.dot(actual);
    REQUIRE(std::abs(overlap) > 0.0);
    const ecd::Complex phase = overlap / std::abs(overlap);
    return (actual - phase * target).norm();
}

double phase_aligned_unitary_error(const ecd::Matrix& target,
                                   const ecd::Matrix& actual) {
    const ecd::Complex overlap =
        target.conjugate().cwiseProduct(actual).sum();
    REQUIRE(std::abs(overlap) > 0.0);
    const ecd::Complex phase = overlap / std::abs(overlap);
    return (actual - phase * target).norm();
}

}

TEST_CASE("Torch ECD finder prepares a nontrivial state end to end",
          "[integration][ecd][torch]") {
    constexpr int d = 2;
    constexpr int N = 4;
    constexpr int k = 4;

    const double inv_sqrt_two = 1.0 / std::sqrt(2.0);
    ecd::Vector target(2);
    target << inv_sqrt_two, ecd::Complex(0.0, inv_sqrt_two);

    const ecd::ECDOptions options = optimizer_options();
    const ecd::ECDParameterFinder finder(d, 1, options);
    const auto circuit = finder.find_state_parameters(target, k, N, 1701);

    REQUIRE(circuit.has_value());
    const ecd::Matrix embedding = ecd::logical_embedding(d, 1, N);
    const ecd::Vector padded_target = embedding * target;
    const ecd::CircuitResult replay =
        ecd::run_circuit(circuit->betas, circuit->rotations, N);
    const double replay_error =
        ecd::state_infidelity(padded_target, replay.psi_g);
    const double aligned_error =
        phase_aligned_state_error(padded_target, replay.psi_g);
    const double ground_return = replay.psi_g.squaredNorm();
    const double logical_leakage =
        state_logical_leakage(replay.psi_g, embedding);

    CAPTURE(circuit->err, replay_error, aligned_error, ground_return,
            logical_leakage, replay.boundary);
    CHECK(replay_error == Catch::Approx(circuit->err).margin(1e-11));
    CHECK(replay.boundary ==
          Catch::Approx(circuit->boundary_leakage).margin(1e-11));
    CHECK(replay_error < 5e-4);
    CHECK(aligned_error < 0.04);
    CHECK(ground_return > 0.999);
    CHECK(logical_leakage < 5e-4);
    CHECK(replay.boundary < 0.01);

    constexpr int larger_N = N + 2;
    const ecd::Matrix larger_embedding =
        ecd::logical_embedding(d, 1, larger_N);
    const ecd::Vector larger_target = larger_embedding * target;
    const ecd::CircuitResult larger_replay =
        ecd::run_circuit(circuit->betas, circuit->rotations, larger_N);
    const double larger_error =
        ecd::state_infidelity(larger_target, larger_replay.psi_g);
    const double larger_leakage =
        state_logical_leakage(larger_replay.psi_g, larger_embedding);

    CAPTURE(larger_error, larger_leakage, larger_replay.boundary);
    CHECK(larger_error < 0.005);
    CHECK(larger_replay.psi_g.squaredNorm() > 0.995);
    CHECK(larger_leakage < 0.004);
}

TEST_CASE("Torch ECD finder synthesizes Pauli X end to end",
          "[integration][ecd][torch]") {
    constexpr int d = 2;
    constexpr int N = 4;
    constexpr int k = 8;

    ecd::Matrix target(2, 2);
    target << 0.0, 1.0,
              1.0, 0.0;

    const ecd::ECDOptions options = optimizer_options();
    const ecd::ECDParameterFinder finder(d, 1, options);
    const auto circuit = finder.find_unitary_parameters(target, k, N, 2903);

    REQUIRE(circuit.has_value());
    const ecd::Matrix embedding = ecd::logical_embedding(d, 1, N);
    const ecd::UnitaryCircuitResult replay =
        ecd::run_unitary_circuit(circuit->betas, circuit->rotations, d, N);
    const ecd::Matrix padded_target = embedding * target;
    const ecd::Matrix logical_block = embedding.adjoint() * replay.ground_block;
    const double replay_error =
        ecd::unitary_infidelity(padded_target, replay.ground_block);
    const double aligned_error =
        phase_aligned_unitary_error(target, logical_block);
    const double logical_leakage =
        unitary_logical_leakage(replay.ground_block, embedding);
    const double isometry_error =
        (logical_block.adjoint() * logical_block -
         ecd::Matrix::Identity(d, d)).norm();
    const double worst_ground_return = std::min(
        replay.ground_block.col(0).squaredNorm(),
        replay.ground_block.col(1).squaredNorm());

    CAPTURE(circuit->err, replay_error, aligned_error, logical_leakage,
            isometry_error, worst_ground_return, replay.boundary);
    CHECK(replay_error == Catch::Approx(circuit->err).margin(1e-11));
    CHECK(replay.boundary ==
          Catch::Approx(circuit->boundary_leakage).margin(1e-11));
    CHECK(replay_error < 5e-4);
    CHECK(aligned_error < 0.04);
    CHECK(worst_ground_return > 0.998);
    CHECK(logical_leakage < 1e-3);
    CHECK(isometry_error < 0.004);
    CHECK(replay.boundary < 0.005);

    constexpr int larger_N = N + 2;
    const ecd::Matrix larger_embedding =
        ecd::logical_embedding(d, 1, larger_N);
    const ecd::UnitaryCircuitResult larger_replay =
        ecd::run_unitary_circuit(circuit->betas, circuit->rotations, d,
                                 larger_N);
    const ecd::Matrix larger_target = larger_embedding * target;
    const double larger_error =
        ecd::unitary_infidelity(larger_target, larger_replay.ground_block);
    const double larger_leakage =
        unitary_logical_leakage(larger_replay.ground_block,
                                larger_embedding);

    CAPTURE(larger_error, larger_leakage, larger_replay.boundary);
    CHECK(larger_error < 0.006);
    CHECK(larger_leakage < 0.004);
}

#endif
