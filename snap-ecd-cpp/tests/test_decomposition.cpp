#include <algorithm>
#include <cmath>
#include <string>
#include <variant>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "decomposition.hpp"
#include "ecd_parameter_finder.hpp"
#include "snap_parameter_finder.hpp"

namespace {

decomp::Options fixed_options(int layers = 0, int buffers = 0) {
    decomp::Options options;
    options.layers = layers;
    options.buffers = buffers;
    options.max_restarts = 1;
    options.optimization_threshold = 1e-3;
    options.stability_threshold = 1e-3;
    options.lbfgs_max_iterations = 400;
    options.ecd.penalty_weight = 0.0;
    options.ecd.gradient_method = decomp::GradientMethod::FiniteDifference;
    return options;
}

}

TEST_CASE("unified decomposition rebuilds the identity with either gate set") {
    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);
    for (const decomp::GateSet gate_set : {decomp::GateSet::Snap,
                                           decomp::GateSet::Ecd}) {
        const decomp::Result result =
            decomp::decompose(identity, 1, gate_set, fixed_options());
        CHECK(result.gate_set == gate_set);
        CHECK(result.converged);
        CHECK(result.error <= 1e-3);
        CHECK(result.replay_error <= 1e-3);
        CHECK(result.layers == 0);
        CHECK(result.buffers == 0);
        CHECK(result.cutoff_per_mode == 2);
        CHECK(result.restarts_used == 1);
        CHECK(result.runtime_seconds >= 0.0);
        if (gate_set == decomp::GateSet::Snap) {
            const auto& circuit =
                std::get<decomp::SnapCircuit>(result.circuit);
            CHECK(circuit.alphas.size() == 1);
            CHECK(circuit.thetas.rows() == 0);
            CHECK(circuit.thetas.cols() == 2);
        } else {
            const auto& circuit = std::get<decomp::ECDCircuit>(result.circuit);
            CHECK(circuit.betas.rows() == 0);
            CHECK(circuit.betas.cols() == 1);
            CHECK(circuit.rotations.rows() == 1);
            CHECK(circuit.rotations.cols() == 2);
        }
    }
}

TEST_CASE("unified decomposition records the complete replay curve") {
    const decomp::Options defaults;
    CHECK(defaults.replay_increment == 12);

    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);
    const decomp::Result result = decomp::decompose(
        identity, 1, decomp::GateSet::Snap, fixed_options());

    REQUIRE(result.replay_checks.size() == 12);
    double worst_error = 0.0;
    for (std::size_t i = 0; i < result.replay_checks.size(); ++i) {
        const decomp::ReplayCheck& check = result.replay_checks[i];
        CHECK(check.cutoff_per_mode == result.cutoff_per_mode +
                                            static_cast<int>(i) + 1);
        worst_error = std::max(worst_error, check.error);
    }
    CHECK(result.replay_error == Catch::Approx(worst_error).margin(1e-15));
}

TEST_CASE("a rejected training-converged candidate does not stop restarts") {
    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);

    SECTION("SNAP") {
        snap::SnapOptions options;
        options.max_runs = 2;
        options.err_th = 1e-8;
        int candidates_seen = 0;
        options.candidate_acceptor = [&](const snap::PulseResult& candidate) {
            CHECK(candidate.err <= options.err_th);
            ++candidates_seen;
            return candidates_seen == 2;
        };

        const snap::PulseResult result =
            snap::pulse_parameter_finder_unitary(identity, 0, 2, options);
        CHECK(result.converged);
        CHECK(result.runs_used == 2);
        CHECK(candidates_seen == 2);
    }

    SECTION("ECD") {
        ecd::ECDOptions options;
        options.n_restarts = 2;
        options.err_th = 1e-8;
        int candidates_seen = 0;
        options.candidate_acceptor =
            [&](const ecd::CompiledCircuit& candidate) {
                CHECK(candidate.err <= options.err_th);
                ++candidates_seen;
                return candidates_seen == 2;
            };

        const ecd::ECDParameterFinder finder(2, 1, options);
        const ecd::CompiledCircuit result =
            finder.attempt_unitary_parameters(identity, 0, 2, 0);
        CHECK(result.accepted);
        CHECK(result.restarts_used == 2);
        CHECK(candidates_seen == 2);
    }
}

TEST_CASE("unified decomposition is reproducible for a fixed seed") {
    decomp::Options options = fixed_options();
    options.max_restarts = 2;
    options.seed = 1234;
    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);

    const decomp::Result first = decomp::decompose(
        identity, 1, decomp::GateSet::Snap, options);
    const decomp::Result second = decomp::decompose(
        identity, 1, decomp::GateSet::Snap, options);

    const auto& first_circuit = std::get<decomp::SnapCircuit>(first.circuit);
    const auto& second_circuit = std::get<decomp::SnapCircuit>(second.circuit);
    CHECK(first_circuit.alphas.isApprox(second_circuit.alphas, 1e-14));
    CHECK(first_circuit.thetas.isApprox(second_circuit.thetas, 1e-14));
    CHECK(first.replay_error == Catch::Approx(second.replay_error).margin(1e-15));
    CHECK(first.restarts_used == second.restarts_used);
    CHECK(first.iterations == second.iterations);
    CHECK(first.objective_evaluations == second.objective_evaluations);
}

TEST_CASE("unified search retries after an unstable trained candidate") {
    decomp::Options options = fixed_options();
    options.max_restarts = 4;
    options.optimization_threshold = 1e-8;
    options.seed = 6;
    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);

    const decomp::Result result = decomp::decompose(
        identity, 1, decomp::GateSet::Snap, options);

    CHECK(result.converged);
    CHECK(result.restarts_used > 1);
    CHECK(result.restarts_used <= options.max_restarts);
    CHECK(result.error <= options.optimization_threshold);
    CHECK(result.replay_error <= options.stability_threshold);
}

TEST_CASE("decompose vector overload prepares vacuum with either gate set") {
    const decomp::Vector vacuum = decomp::Vector::Unit(2, 0);
    for (const decomp::GateSet gate_set : {decomp::GateSet::Snap,
                                           decomp::GateSet::Ecd}) {
        const decomp::Result result =
            decomp::decompose(vacuum, 1, gate_set, fixed_options());
        CHECK(result.converged);
        CHECK(result.error <= 1e-3);
        CHECK(result.replay_error <= 1e-3);
    }
}

TEST_CASE("unified decomposition accepts Eigen target expressions") {
    const decomp::Result unitary = decomp::decompose(
        decomp::Matrix::Identity(2, 2), 1, decomp::GateSet::Ecd,
        fixed_options());
    const decomp::Result state = decomp::decompose(
        decomp::Vector::Unit(2, 0), 1, decomp::GateSet::Ecd,
        fixed_options());
    CHECK(unitary.replay_error <= 1e-3);
    CHECK(state.replay_error <= 1e-3);
}

TEST_CASE("decomposition infers per-mode levels exactly") {
    const decomp::Matrix identity = decomp::Matrix::Identity(4, 4);
    const decomp::Result result = decomp::decompose(
        identity, 2, decomp::GateSet::Ecd, fixed_options());
    CHECK(result.cutoff_per_mode == 2);
    const auto& circuit = std::get<decomp::ECDCircuit>(result.circuit);
    CHECK(circuit.betas.cols() == 2);

    const decomp::Matrix invalid_dimension = decomp::Matrix::Identity(3, 3);
    CHECK_THROWS_AS(
        decomp::decompose(invalid_dimension, 2, decomp::GateSet::Ecd,
                          fixed_options()),
        std::invalid_argument);
}

TEST_CASE("unitary decomposition ignores global phase") {
    const decomp::Matrix target =
        decomp::Matrix::Identity(2, 2) * core::Complex(0.0, 1.0);
    for (const decomp::GateSet gate_set : {decomp::GateSet::Snap,
                                           decomp::GateSet::Ecd}) {
        const decomp::Result result =
            decomp::decompose(target, 1, gate_set, fixed_options());
        CHECK(result.error == Catch::Approx(0.0).margin(1e-12));
        CHECK(result.replay_error == Catch::Approx(0.0).margin(1e-12));
    }
}

TEST_CASE("fixed layers and buffers are never changed") {
    decomp::Options options = fixed_options(0, 3);
    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);
    const decomp::Result result = decomp::decompose(
        identity, 1, decomp::GateSet::Ecd, options);
    CHECK(result.layers == 0);
    CHECK(result.buffers == 3);
    CHECK(result.cutoff_per_mode == 5);
}

TEST_CASE("automatic sizing starts from comfortable bounded defaults") {
    decomp::Options options;
    options.max_layers = 4;
    options.max_buffers = 2;

    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);
    const decomp::Result result =
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options);
    CHECK(result.layers == 4);
    CHECK(result.buffers == 2);
    CHECK(result.cutoff_per_mode == 4);
    CHECK(result.error <= options.optimization_threshold);
    CHECK(result.replay_error <= options.stability_threshold);
}

TEST_CASE("larger-cutoff replay rejects a truncation-dependent circuit") {
    const double alpha = 0.8;
    const decomp::Matrix target = snap::displacement(alpha, 2);
    decomp::Options options = fixed_options(0, 0);
    options.optimization_threshold = 1e-10;
    options.stability_threshold = 1e-6;

    try {
        static_cast<void>(decomp::decompose(
            target, 1, decomp::GateSet::Snap, options));
        FAIL("expected replay stability failure");
    } catch (const decomp::DecompositionError& error) {
        CHECK(error.gate_set() == decomp::GateSet::Snap);
        CHECK(error.had_training_convergence());
        CHECK(error.best_error() <= options.optimization_threshold);
        // The candidate trained but failed replay stability, so the reported
        // replay error must exceed the stability threshold that rejected it.
        CHECK(error.best_replay_error() > options.stability_threshold);
        REQUIRE(error.attempts().size() == 1);
        CHECK(error.attempts().front().layers == 0);
        CHECK(error.attempts().front().buffers == 0);
        const std::string message = error.what();
        CHECK(message.find("SNAP") != std::string::npos);
        CHECK(message.find("layers=0") != std::string::npos);
        CHECK(message.find("buffers=0") != std::string::npos);
        CHECK(message.find("failed replay stability") != std::string::npos);
    }
}

TEST_CASE("search exhaustion throws the best failed attempt") {
    decomp::Matrix pauli_x(2, 2);
    pauli_x << 0.0, 1.0,
               1.0, 0.0;
    decomp::Options options = fixed_options(0, 0);
    options.optimization_threshold = 1e-8;

    try {
        static_cast<void>(decomp::decompose(
            pauli_x, 1, decomp::GateSet::Ecd, options));
        FAIL("expected search failure");
    } catch (const decomp::DecompositionError& error) {
        CHECK(error.gate_set() == decomp::GateSet::Ecd);
        CHECK_FALSE(error.had_training_convergence());
        // No candidate reached the optimization threshold, so the best
        // infidelity seen across the search must sit above it.
        CHECK(error.best_error() > options.optimization_threshold);
        REQUIRE(error.attempts().size() == 1);
        CHECK(error.attempts().front().layers == 0);
        CHECK(error.attempts().front().buffers == 0);
        const std::string message = error.what();
        CHECK(message.find("ECD") != std::string::npos);
        CHECK(message.find("layers=0") != std::string::npos);
        CHECK(message.find("buffers=0") != std::string::npos);
        CHECK(message.find("best infidelity=") != std::string::npos);
    }
}

TEST_CASE("unified decomposition validates targets and common options") {
    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);
    CHECK_THROWS_AS(
        decomp::decompose(identity, 0, decomp::GateSet::Ecd),
        std::invalid_argument);
    CHECK_THROWS_AS(
        decomp::decompose(identity, 2, decomp::GateSet::Snap),
        std::invalid_argument);

    decomp::Matrix not_unitary = identity;
    not_unitary(0, 0) = 2.0;
    CHECK_THROWS_AS(
        decomp::decompose(not_unitary, 1, decomp::GateSet::Ecd),
        std::invalid_argument);

    decomp::Vector not_normalized(2);
    not_normalized << 1.0, 1.0;
    CHECK_THROWS_AS(
        decomp::decompose(not_normalized, 1, decomp::GateSet::Ecd),
        std::invalid_argument);

    decomp::Options options = fixed_options();
    options.layers = -1;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options),
        std::invalid_argument);

    options = fixed_options();
    options.buffers = -1;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options),
        std::invalid_argument);

    options = fixed_options();
    options.optimization_threshold = -1.0;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options),
        std::invalid_argument);

    options = fixed_options();
    options.replay_increment = 0;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options),
        std::invalid_argument);

    options = fixed_options();
    options.replay_increment = 11;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options),
        std::invalid_argument);

    options = fixed_options();
    options.snap.n_penalize = -2;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Snap, options),
        std::invalid_argument);

    options = fixed_options();
    options.snap.penalty_weight = -0.1;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Snap, options),
        std::invalid_argument);

    options = fixed_options();
    options.layers.reset();
    options.ecd.warm_start_layers = 2;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options),
        std::invalid_argument);

    options = fixed_options();
    options.ecd.warm_start_layers = *options.layers;
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options),
        std::invalid_argument);
}

#ifndef ECD_WITH_TORCH
TEST_CASE("explicit unavailable autodiff is rejected") {
    decomp::Options options = fixed_options();
    options.ecd.gradient_method = decomp::GradientMethod::Autodiff;
    const decomp::Matrix identity = decomp::Matrix::Identity(2, 2);
    CHECK_THROWS_AS(
        decomp::decompose(identity, 1, decomp::GateSet::Ecd, options),
        std::invalid_argument);
}
#endif
