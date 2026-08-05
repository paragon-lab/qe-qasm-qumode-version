#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "decomposition.hpp"

namespace {

decomp::Matrix fourier_gate(int dimension) {
    const double scale = 1.0 / std::sqrt(static_cast<double>(dimension));
    const double two_pi = 2.0 * std::acos(-1.0);
    decomp::Matrix target(dimension, dimension);
    for (int row = 0; row < dimension; ++row) {
        for (int column = 0; column < dimension; ++column) {
            const double angle = two_pi * row * column / dimension;
            target(row, column) =
                scale * std::exp(core::Complex(0.0, angle));
        }
    }
    return target;
}

void check_acceptance(const decomp::Result& result) {
    CHECK(result.converged);
    CHECK(result.error <= 1e-3);
    CHECK(result.replay_error <= 1e-2);
    REQUIRE(result.replay_checks.size() == 12);
    for (const decomp::ReplayCheck& check : result.replay_checks) {
        CHECK(check.error <= 1e-2);
    }
    CHECK(std::isfinite(result.boundary_leakage));
    CHECK(result.runtime_seconds < 60.0);
}

}

TEST_CASE("dimension-4 Fourier is cutoff-stable with SNAP",
          "[acceptance][fourier][snap]") {
    decomp::Options options;
    options.layers = 6;
    options.buffers = 10;
    options.max_restarts = 5;
    options.optimization_threshold = 1e-3;
    options.stability_threshold = 1e-2;
    options.lbfgs_max_iterations = 3000;
    options.seed = 404;
    options.snap.n_penalize = 5;
    options.snap.penalty_weight = 0.1;

    const decomp::Result result = decomp::decompose(
        fourier_gate(4), 1, decomp::GateSet::Snap, options);
    check_acceptance(result);
}

TEST_CASE("dimension-4 Fourier is cutoff-stable with finite-difference ECD",
          "[acceptance][fourier][ecd]") {
    decomp::Options options;
    options.layers = 28;
    options.buffers = 10;
    options.max_restarts = 1;
    options.optimization_threshold = 1e-3;
    options.stability_threshold = 1e-2;
    options.lbfgs_tolerance = 1e-8;
    options.lbfgs_max_iterations = 2500;
    options.seed = 804;
    options.ecd.n_penalize = 5;
    options.ecd.penalty_weight = 0.01;
    options.ecd.gradient_method = decomp::GradientMethod::FiniteDifference;
    options.ecd.finite_difference_step = 1e-6;
    options.ecd.warm_start_layers = 24;

    const decomp::Result result = decomp::decompose(
        fourier_gate(4), 1, decomp::GateSet::Ecd, options);
    check_acceptance(result);
}
