#include <catch2/catch_test_macros.hpp>

#include "snap_parameter_finder.hpp"

namespace {

constexpr double tolerance = 1e-12;

}

TEST_CASE("SNAP parameter packing round trips in layer-major order") {
    Eigen::VectorXd alphas(3);
    alphas << 0.13, -0.27, 0.41;
    Eigen::MatrixXd thetas(2, 3);
    thetas << 0.2, -0.4, 0.6,
              0.8, -1.0, 1.2;

    const Eigen::VectorXd params =
        snap::alphas_and_thetas_to_params(alphas, thetas);

    REQUIRE(params.size() == 9);
    CHECK(snap::params_to_alphas(params, 2, 3).isApprox(alphas, tolerance));
    CHECK(snap::params_to_thetas(params, 2, 3).isApprox(thetas, tolerance));
    CHECK(params(4) == thetas(0, 1));
    CHECK(params(7) == thetas(1, 1));
}

TEST_CASE("zero-layer SNAP ansatz is a single displacement") {
    const snap::DisplacementBasis basis(6);
    Eigen::VectorXd alphas(1);
    alphas << 0.38;
    const Eigen::MatrixXd thetas(0, 4);

    CHECK(snap::ansatz_unitary(basis, alphas, thetas)
              .isApprox(snap::displacement(0.38, 6), tolerance));
}

TEST_CASE("SNAP state ansatz equals the unitary action on vacuum") {
    const snap::DisplacementBasis basis(7);
    Eigen::VectorXd alphas(3);
    alphas << 0.31, -0.22, 0.17;
    Eigen::MatrixXd thetas(2, 5);
    thetas << 0.1, -0.3, 0.5, -0.7, 0.9,
              -0.2, 0.4, -0.6, 0.8, -1.0;

    const snap::Matrix unitary = snap::ansatz_unitary(basis, alphas, thetas);
    const snap::Vector vacuum = snap::Vector::Unit(7, 0);
    const snap::Vector state = snap::ansatz_state(basis, alphas, thetas);

    CHECK(state.isApprox(unitary * vacuum, tolerance));
    CHECK(std::abs(state.squaredNorm() - 1.0) < tolerance);
}

TEST_CASE("SNAP ansatz applies gates in displacement-phase order") {
    const snap::DisplacementBasis basis(5);
    Eigen::VectorXd alphas(3);
    alphas << 0.24, -0.36, 0.19;
    Eigen::MatrixXd thetas(2, 4);
    thetas << 0.2, -0.5, 0.7, -0.9,
              -0.3, 0.6, -0.8, 1.1;

    const snap::Matrix phase_zero =
        snap::snap_diagonal(thetas.row(0).transpose(), 5).asDiagonal();
    const snap::Matrix phase_one =
        snap::snap_diagonal(thetas.row(1).transpose(), 5).asDiagonal();
    const snap::Matrix expected =
        basis.displacement(alphas(2)) * phase_one *
        basis.displacement(alphas(1)) * phase_zero *
        basis.displacement(alphas(0));
    const snap::Matrix actual = snap::ansatz_unitary(basis, alphas, thetas);

    CHECK(actual.isApprox(expected, tolerance));
    CHECK((actual.adjoint() * actual)
              .isApprox(snap::Matrix::Identity(5, 5), tolerance));
}

TEST_CASE("SNAP ansatz rejects incompatible parameter shapes") {
    const snap::DisplacementBasis basis(5);
    const Eigen::VectorXd short_alphas = Eigen::VectorXd::Zero(2);
    const Eigen::MatrixXd two_layers = Eigen::MatrixXd::Zero(2, 4);
    const Eigen::VectorXd alphas = Eigen::VectorXd::Zero(3);
    const Eigen::MatrixXd too_many_levels = Eigen::MatrixXd::Zero(2, 6);

    CHECK_THROWS_AS(snap::ansatz_unitary(basis, short_alphas, two_layers),
                    std::invalid_argument);
    CHECK_THROWS_AS(snap::ansatz_state(basis, alphas, too_many_levels),
                    std::invalid_argument);
}

