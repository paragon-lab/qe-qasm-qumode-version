#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ecd_parameter_finder.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("ECD finder prepares a phase-tagged two-mode Bell state") {
    constexpr int d = 2;
    constexpr int num_modes = 2;
    constexpr int k = 4;
    constexpr int training_cutoff = 5;
    constexpr int replay_cutoff = 7;

    ecd::Vector target = ecd::Vector::Zero(4);
    target(1) = 1.0 / std::sqrt(2.0);
    target(2) = ecd::Complex(0.0, 1.0 / std::sqrt(2.0));

    ecd::ECDOptions options;
    options.err_th = 0.01;
    options.n_restarts = 1;
    options.lbfgs_max_iter = 200;
    options.grad_method = ecd::GradMethod::FiniteDifference;

    const ecd::ECDParameterFinder finder(d, num_modes, options);
    const auto circuit =
        finder.find_state_parameters(target, k, training_cutoff, 3);

    REQUIRE(circuit.has_value());
    REQUIRE(circuit->betas.rows() == k);
    REQUIRE(circuit->betas.cols() == num_modes);
    REQUIRE(circuit->rotations.rows() == k * num_modes + 1);

    const ecd::CircuitResult training_replay = ecd::run_circuit(
        circuit->betas, circuit->rotations, training_cutoff);
    const ecd::Matrix training_embedding =
        ecd::logical_embedding(d, num_modes, training_cutoff);
    const ecd::Vector padded_target = training_embedding * target;
    const ecd::Vector logical_state =
        training_embedding.adjoint() * training_replay.psi_g;
    const ecd::Vector logical_residual =
        training_replay.psi_g - training_embedding * logical_state;
    const double replay_error =
        ecd::state_infidelity(padded_target, training_replay.psi_g);

    CHECK_THAT(circuit->err, WithinAbs(replay_error, 1e-12));
    CHECK_THAT(circuit->boundary_leakage,
               WithinAbs(training_replay.boundary, 1e-12));
    CHECK(replay_error < options.err_th);
    CHECK(training_replay.psi_g.squaredNorm() > 0.99);
    CHECK(logical_residual.squaredNorm() < 0.01);
    CHECK(std::abs(logical_state(0)) < 0.03);
    CHECK(std::abs(logical_state(1)) > 0.65);
    CHECK(std::abs(logical_state(2)) > 0.65);
    CHECK(std::abs(logical_state(3)) < 0.03);

    ecd::Vector mode_swapped_target = ecd::Vector::Zero(4);
    mode_swapped_target(1) =
        ecd::Complex(0.0, 1.0 / std::sqrt(2.0));
    mode_swapped_target(2) = 1.0 / std::sqrt(2.0);
    CHECK(std::abs(mode_swapped_target.dot(logical_state)) < 0.05);

    const ecd::CircuitResult larger_cutoff_replay = ecd::run_circuit(
        circuit->betas, circuit->rotations, replay_cutoff);
    const ecd::Matrix larger_embedding =
        ecd::logical_embedding(d, num_modes, replay_cutoff);
    const ecd::Vector larger_target = larger_embedding * target;
    const ecd::Vector larger_logical_state =
        larger_embedding.adjoint() * larger_cutoff_replay.psi_g;
    const ecd::Vector larger_logical_residual =
        larger_cutoff_replay.psi_g -
        larger_embedding * larger_logical_state;
    const double larger_cutoff_error =
        ecd::state_infidelity(larger_target, larger_cutoff_replay.psi_g);

    CHECK(larger_cutoff_error < 0.012);
    CHECK(std::abs(larger_cutoff_error - replay_error) < 0.002);
    CHECK(larger_cutoff_replay.psi_g.squaredNorm() > 0.99);
    CHECK(larger_logical_residual.squaredNorm() < 0.012);
}
