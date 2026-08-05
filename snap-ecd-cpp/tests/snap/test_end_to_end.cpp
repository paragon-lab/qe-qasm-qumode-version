#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "snap_parameter_finder.hpp"

namespace {

snap::Vector coherent_state(double alpha, int dimension) {
    snap::Vector state(dimension);
    double amplitude = std::exp(-alpha * alpha / 2.0);
    state(0) = amplitude;
    for (int level = 1; level < dimension; ++level) {
        amplitude *= alpha / std::sqrt(static_cast<double>(level));
        state(level) = amplitude;
    }
    state.normalize();
    return state;
}

snap::Vector yurke_stoler_cat(double alpha, int dimension) {
    const snap::Vector positive = coherent_state(alpha, dimension);
    const snap::Vector negative = coherent_state(-alpha, dimension);
    snap::Vector target =
        (positive + snap::Complex(0.0, 1.0) * negative) / std::sqrt(2.0);
    target.normalize();
    return target;
}

Eigen::VectorXd cat_alphas(double alpha) {
    Eigen::VectorXd alphas(2);
    alphas << alpha, 0.0;
    return alphas;
}

Eigen::MatrixXd cat_phases(int dimension) {
    const double quarter_pi = std::acos(-1.0) / 4.0;
    Eigen::MatrixXd phases(1, dimension);
    for (int level = 0; level < dimension; ++level) {
        phases(0, level) = level % 2 == 0 ? quarter_pi : -quarter_pi;
    }
    return phases;
}

snap::SnapOptions single_start_options(const Eigen::VectorXd& initial_guess) {
    snap::SnapOptions options;
    options.max_runs = 1;
    options.err_th = 1e-9;
    options.lbfgs_tol = 1e-10;
    options.lbfgs_max_iter = 3000;
    options.use_initial_guess = true;
    options.initial_guess = initial_guess;
    return options;
}

}

TEST_CASE("SNAP state finder recovers a known displacement",
          "[integration][snap]") {
    constexpr int dimension = 10;
    constexpr double expected_alpha = 0.42;
    const snap::Vector target = coherent_state(expected_alpha, dimension);
    Eigen::VectorXd initial_guess(1);
    initial_guess << -0.15;
    const snap::SnapOptions options = single_start_options(initial_guess);

    const snap::PulseResult result =
        snap::pulse_parameter_finder_state(target, 0, dimension, options);

    REQUIRE(result.converged);
    REQUIRE(result.params.size() == 1);
    const snap::DisplacementBasis basis(dimension);
    const Eigen::MatrixXd no_phases(0, dimension);
    const snap::Vector replay =
        snap::ansatz_state(basis, result.alphas(0, dimension), no_phases);

    CAPTURE(result.err, result.params(0));
    CHECK(result.err < options.err_th);
    CHECK(std::abs(result.params(0) - expected_alpha) < 1e-5);
    CHECK(1.0 - std::abs(target.dot(replay)) < options.err_th);
}

TEST_CASE("SNAP unitary finder recovers a known two-level displacement",
          "[integration][snap]") {
    constexpr int dimension = 2;
    constexpr double expected_alpha = -0.37;
    snap::Matrix target(2, 2);
    target << std::cos(expected_alpha), -std::sin(expected_alpha),
              std::sin(expected_alpha),  std::cos(expected_alpha);
    Eigen::VectorXd initial_guess(1);
    initial_guess << 0.11;
    const snap::SnapOptions options = single_start_options(initial_guess);

    const snap::PulseResult result =
        snap::pulse_parameter_finder_unitary(target, 0, dimension, options);

    REQUIRE(result.converged);
    REQUIRE(result.params.size() == 1);
    const snap::DisplacementBasis basis(dimension);
    const Eigen::MatrixXd no_phases(0, dimension);
    const snap::Matrix replay =
        snap::ansatz_unitary(basis, result.alphas(0, dimension), no_phases);

    CAPTURE(result.err, result.params(0));
    CHECK(result.err < options.err_th);
    CHECK(std::abs(result.params(0) - expected_alpha) < 1e-5);
    CHECK((replay - target).cwiseAbs().maxCoeff() < 1e-5);
}

TEST_CASE("known SNAP parity phases prepare a Yurke-Stoler cat state",
          "[integration][snap]") {
    constexpr int dimension = 14;
    constexpr double alpha = 0.55;
    const snap::Vector target = yurke_stoler_cat(alpha, dimension);
    const Eigen::VectorXd alphas = cat_alphas(alpha);
    const Eigen::MatrixXd phases = cat_phases(dimension);
    const snap::DisplacementBasis basis(dimension);

    const snap::Vector replay = snap::ansatz_state(basis, alphas, phases);
    const double error = 1.0 - std::abs(target.dot(replay));

    CAPTURE(error);
    CHECK(error < 1e-11);
    CHECK(std::abs(replay.squaredNorm() - 1.0) < 1e-12);
    CHECK(std::abs(alphas(0) - alpha) < 1e-15);
    CHECK(std::abs(alphas(1)) < 1e-15);
    for (int level = 0; level < dimension; ++level) {
        const double expected =
            (level % 2 == 0 ? 1.0 : -1.0) * std::acos(-1.0) / 4.0;
        CHECK(std::abs(phases(0, level) - expected) < 1e-15);
    }
}

TEST_CASE("SNAP state finder converges from a perturbed cat sequence",
          "[integration][snap]") {
    constexpr int dimension = 10;
    constexpr double alpha = 0.5;
    const snap::Vector target = yurke_stoler_cat(alpha, dimension);
    const Eigen::VectorXd known_alphas = cat_alphas(alpha);
    const Eigen::MatrixXd known_phases = cat_phases(dimension);
    Eigen::VectorXd initial_guess =
        snap::alphas_and_thetas_to_params(known_alphas, known_phases);
    initial_guess(0) -= 0.04;
    initial_guess(1) += 0.03;
    for (int index = 2; index < initial_guess.size(); ++index) {
        initial_guess(index) += 0.02 * std::sin(static_cast<double>(index));
    }
    snap::SnapOptions options = single_start_options(initial_guess);
    options.n_levels = dimension;
    options.err_th = 2e-8;

    const snap::PulseResult result =
        snap::pulse_parameter_finder_state(target, 1, dimension, options);

    CAPTURE(result.err, result.runs_used);
    REQUIRE(result.converged);
    const snap::DisplacementBasis basis(dimension);
    const snap::Vector replay = snap::ansatz_state(
        basis, result.alphas(1, dimension), result.thetas(1, dimension));
    const double replay_error = 1.0 - std::abs(target.dot(replay));

    CAPTURE(replay_error);
    CHECK(result.runs_used == 1);
    CHECK(result.err < options.err_th);
    CHECK(std::abs(result.err - replay_error) < 1e-12);
}
