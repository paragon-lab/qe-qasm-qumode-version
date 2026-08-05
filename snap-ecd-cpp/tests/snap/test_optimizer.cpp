#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "snap_parameter_finder.hpp"

TEST_CASE("SNAP pulse result exposes alphas and layer-major phases") {
    snap::PulseResult result;
    result.params.resize(7);
    result.params << 0.1, -0.2, 0.3,
                     0.4, 0.5, 0.6,
                     0.7;

    Eigen::VectorXd expected_alphas(3);
    expected_alphas << 0.1, -0.2, 0.3;
    Eigen::MatrixXd expected_thetas(2, 2);
    expected_thetas << 0.4, 0.5,
                       0.6, 0.7;

    CHECK(result.alphas(2, 2).isApprox(expected_alphas, 1e-15));
    CHECK(result.thetas(2, 2).isApprox(expected_thetas, 1e-15));
}

TEST_CASE("SNAP state finder reports an unreachable shallow target") {
    constexpr int dimension = 6;
    const snap::Vector target = snap::Vector::Unit(dimension, 1);
    snap::SnapOptions options;
    options.max_runs = 1;
    options.err_th = 0.1;
    options.lbfgs_max_iter = 300;
    options.seed = 4;

    const snap::PulseResult result =
        snap::pulse_parameter_finder_state(target, 0, dimension, options);

    CHECK_FALSE(result.converged);
    CHECK(result.runs_used == 1);
    CHECK(result.params.size() == 1);
    CHECK(std::isfinite(result.err));
    CHECK(result.err > options.err_th);
}

TEST_CASE("SNAP finders validate target and level dimensions") {
    constexpr int dimension = 4;
    snap::SnapOptions options;
    options.n_levels = dimension + 1;
    const snap::Vector state = snap::Vector::Unit(dimension, 0);
    const snap::Vector unnormalized = 2.0 * state;
    const snap::Matrix unitary = snap::Matrix::Identity(dimension, dimension);

    CHECK_THROWS_AS(snap::pulse_parameter_finder_state(
                        unnormalized, 1, dimension),
                    std::invalid_argument);
    CHECK_THROWS_AS(snap::pulse_parameter_finder_state(
                        state, 1, dimension, options),
                    std::invalid_argument);
    CHECK_THROWS_AS(snap::pulse_parameter_finder_unitary(
                        unitary, 1, dimension, options),
                    std::invalid_argument);
    CHECK_THROWS_AS(snap::pulse_parameter_finder_unitary(
                        snap::Matrix::Identity(3, 3), 1, dimension),
                    std::invalid_argument);

    options.n_levels = dimension;
    options.d_fid = dimension + 1;
    CHECK_THROWS_AS(snap::pulse_parameter_finder_unitary(
                        unitary, 1, dimension, options),
                    std::invalid_argument);
    options.d_fid = -2;
    CHECK_THROWS_AS(snap::pulse_parameter_finder_unitary(
                        unitary, 1, dimension, options),
                    std::invalid_argument);
}

TEST_CASE("SNAP penalty reduces boundary occupation") {
    constexpr int dimension = 6;
    const snap::DisplacementBasis basis(dimension);
    Eigen::VectorXd target_alpha(1);
    target_alpha << 1.25;
    const Eigen::MatrixXd no_phases(0, dimension);
    const snap::Vector target =
        snap::ansatz_state(basis, target_alpha, no_phases);

    snap::SnapOptions unpenalized;
    unpenalized.max_runs = 1;
    unpenalized.err_th = 1.0;
    unpenalized.lbfgs_max_iter = 1000;
    unpenalized.use_initial_guess = true;
    unpenalized.initial_guess = Eigen::VectorXd::Constant(1, 0.2);
    unpenalized.n_penalize = 2;

    snap::SnapOptions penalized = unpenalized;
    penalized.penalty_weight = 2.0;

    const snap::PulseResult baseline =
        snap::pulse_parameter_finder_state(target, 0, dimension,
                                           unpenalized);
    const snap::PulseResult regularized =
        snap::pulse_parameter_finder_state(target, 0, dimension, penalized);

    CAPTURE(baseline.err, baseline.boundary_leakage, regularized.err,
            regularized.boundary_leakage);
    CHECK(regularized.boundary_leakage < baseline.boundary_leakage);
}
