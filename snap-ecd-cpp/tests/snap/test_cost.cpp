#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "snap_parameter_finder.hpp"

using Catch::Matchers::WithinAbs;

namespace {

struct CircuitFixture {
    snap::DisplacementBasis basis{6};
    Eigen::VectorXd alphas{3};
    Eigen::MatrixXd thetas{2, 5};

    CircuitFixture() {
        alphas << 0.23, -0.31, 0.18;
        thetas << 0.2, -0.4, 0.6, -0.8, 1.0,
                  -0.3, 0.5, -0.7, 0.9, -1.1;
    }
};

}

TEST_CASE("SNAP state cost vanishes for the realized state") {
    const CircuitFixture fixture;
    const snap::Vector target =
        snap::ansatz_state(fixture.basis, fixture.alphas, fixture.thetas);

    CHECK_THAT(snap::cost_state(target, fixture.basis, fixture.alphas,
                                fixture.thetas),
               WithinAbs(0.0, 1e-12));
}

TEST_CASE("SNAP state cost ignores global phase") {
    const CircuitFixture fixture;
    const snap::Vector state =
        snap::ansatz_state(fixture.basis, fixture.alphas, fixture.thetas);
    const snap::Vector target =
        std::exp(snap::Complex(0.0, 0.73)) * state;

    CHECK_THAT(snap::cost_state(target, fixture.basis, fixture.alphas,
                                fixture.thetas),
               WithinAbs(0.0, 1e-12));
}

TEST_CASE("SNAP state cost validates target dimensions and normalization") {
    const CircuitFixture fixture;
    const snap::Vector wrong_size = snap::Vector::Unit(5, 0);
    const snap::Vector unnormalized = 2.0 * snap::Vector::Unit(6, 0);

    CHECK_THROWS_AS(snap::cost_state(wrong_size, fixture.basis,
                                     fixture.alphas, fixture.thetas),
                    std::invalid_argument);
    CHECK_THROWS_AS(snap::cost_state(unnormalized, fixture.basis,
                                     fixture.alphas, fixture.thetas),
                    std::invalid_argument);
}

TEST_CASE("SNAP unitary cost vanishes and ignores global phase") {
    const CircuitFixture fixture;
    const snap::Matrix realized =
        snap::ansatz_unitary(fixture.basis, fixture.alphas, fixture.thetas);
    const snap::Matrix phased =
        std::exp(snap::Complex(0.0, -0.47)) * realized;

    CHECK_THAT(snap::cost_unitary(realized, fixture.basis, fixture.alphas,
                                  fixture.thetas),
               WithinAbs(0.0, 1e-12));
    CHECK_THAT(snap::cost_unitary(phased, fixture.basis, fixture.alphas,
                                  fixture.thetas),
               WithinAbs(0.0, 1e-12));
}

TEST_CASE("SNAP unitary cost can restrict fidelity to a leading subspace") {
    const snap::DisplacementBasis basis(4);
    Eigen::VectorXd alphas(1);
    alphas << 0.0;
    const Eigen::MatrixXd thetas(0, 4);
    snap::Matrix target = snap::Matrix::Identity(4, 4);
    target(2, 2) = -1.0;
    target(3, 3) = -1.0;

    CHECK_THAT(snap::cost_unitary(target, basis, alphas, thetas, 2),
               WithinAbs(0.0, 1e-12));
    CHECK_THAT(snap::cost_unitary(target, basis, alphas, thetas),
               WithinAbs(1.0, 1e-12));
}

TEST_CASE("SNAP unitary cost rejects an incompatible target dimension") {
    const CircuitFixture fixture;
    const snap::Matrix target = snap::Matrix::Identity(5, 5);

    CHECK_THROWS_AS(snap::cost_unitary(target, fixture.basis,
                                       fixture.alphas, fixture.thetas),
                    std::invalid_argument);
}

TEST_CASE("SNAP unitary cost validates the fidelity dimension") {
    const CircuitFixture fixture;
    const snap::Matrix target = snap::Matrix::Identity(6, 6);

    CHECK_THROWS_AS(snap::cost_unitary(target, fixture.basis,
                                       fixture.alphas, fixture.thetas, 0),
                    std::invalid_argument);
    CHECK_THROWS_AS(snap::cost_unitary(target, fixture.basis,
                                       fixture.alphas, fixture.thetas, 7),
                    std::invalid_argument);
}

TEST_CASE("SNAP boundary metrics pin the hard-cutoff penalty and leakage") {
    const snap::DisplacementBasis basis(4);
    Eigen::VectorXd alphas(1);
    alphas << 0.9;
    const Eigen::MatrixXd no_phases(0, 4);

    // With no phase layers the circuit is a single displacement of the initial
    // states, so both metrics are closed forms of D(0.9). n_penalize=2 weights
    // the top two Fock levels by exp(level+1-dimension): exp(-1) and exp(0)=1.
    const snap::Matrix displaced = basis.displacement(0.9);
    const double w_next = std::exp(-1.0);  // level 2 (dimension-2)
    const double w_top = 1.0;              // level 3 (dimension-1)

    const double d20 = std::norm(displaced(2, 0));
    const double d30 = std::norm(displaced(3, 0));
    const double d21 = std::norm(displaced(2, 1));
    const double d31 = std::norm(displaced(3, 1));

    // State target starts from the vacuum |0>: the single displaced column is
    // D(0.9)|0>. Leakage is the top-row occupation; penalty is the weighted sum.
    const double expected_state_leakage = d30;
    const double expected_state_penalty = w_next * d20 + w_top * d30;

    // Unitary target with d_fid=2 propagates the two leading logical columns,
    // averaging the penalty over the two columns.
    const double expected_unitary_leakage = std::max(d30, d31);
    const double expected_unitary_penalty =
        (w_next * (d20 + d21) + w_top * (d30 + d31)) / 2.0;

    const snap::BoundaryMetrics state =
        snap::boundary_metrics_state(basis, alphas, no_phases, 2);
    const snap::BoundaryMetrics unitary =
        snap::boundary_metrics_unitary(basis, alphas, no_phases, 2, 2);

    CHECK_THAT(state.leakage, WithinAbs(expected_state_leakage, 1e-12));
    CHECK_THAT(state.penalty, WithinAbs(expected_state_penalty, 1e-12));
    CHECK_THAT(unitary.leakage, WithinAbs(expected_unitary_leakage, 1e-12));
    CHECK_THAT(unitary.penalty, WithinAbs(expected_unitary_penalty, 1e-12));
}

TEST_CASE("zero SNAP penalty weight preserves the original objectives") {
    const CircuitFixture fixture;
    const snap::Vector state_target = snap::Vector::Unit(6, 0);
    const snap::Matrix unitary_target = snap::Matrix::Identity(6, 6);

    CHECK(snap::objective_state(
              state_target, fixture.basis, fixture.alphas, fixture.thetas,
              2, 0.0) ==
          snap::cost_state(state_target, fixture.basis, fixture.alphas,
                           fixture.thetas));
    CHECK(snap::objective_unitary(
              unitary_target, fixture.basis, fixture.alphas, fixture.thetas,
              2, 0.0, 2) ==
          snap::cost_unitary(unitary_target, fixture.basis, fixture.alphas,
                             fixture.thetas, 2));
}
