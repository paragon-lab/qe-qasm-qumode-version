#include <cmath>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "snap_parameter_finder.hpp"

namespace {

constexpr double matrix_tolerance = 1e-12;

}

TEST_CASE("SNAP displacement at zero is identity") {
    const snap::Matrix actual = snap::displacement(0.0, 6);
    const snap::Matrix expected = snap::Matrix::Identity(6, 6);

    CHECK(actual.isApprox(expected, matrix_tolerance));
}

TEST_CASE("two-level SNAP displacement has the signed closed form") {
    const double alpha = 0.37;
    snap::Matrix expected(2, 2);
    expected << std::cos(alpha), -std::sin(alpha),
                std::sin(alpha),  std::cos(alpha);

    CHECK(snap::displacement(alpha, 2).isApprox(expected,
                                                matrix_tolerance));
}

TEST_CASE("SNAP displacement is unitary") {
    const snap::Matrix displacement = snap::displacement(0.43, 7);
    const snap::Matrix identity = snap::Matrix::Identity(7, 7);

    CHECK((displacement.adjoint() * displacement)
              .isApprox(identity, matrix_tolerance));
}

TEST_CASE("SNAP displacement inverse uses the negative amplitude") {
    const double alpha = -0.37;

    CHECK(snap::displacement(alpha, 6).adjoint().isApprox(
        snap::displacement(-alpha, 6), matrix_tolerance));
}

TEST_CASE("SNAP displacement has the expected infinitesimal generator") {
    constexpr int dimension = 5;
    constexpr double step = 1e-6;
    const Eigen::MatrixXd a = core::annihilation(dimension);
    const snap::Matrix expected = (a.transpose() - a).cast<snap::Complex>();
    const snap::Matrix actual =
        (snap::displacement(step, dimension) -
         snap::displacement(-step, dimension)) /
        (2.0 * step);

    CHECK((actual - expected).cwiseAbs().maxCoeff() < 1e-10);
}

TEST_CASE("SNAP displacement creates coherent-state vacuum amplitudes") {
    constexpr int dimension = 10;
    constexpr double alpha = 0.4;
    const snap::Vector vacuum = snap::Vector::Unit(dimension, 0);
    const snap::Vector actual = snap::displacement(alpha, dimension) * vacuum;

    double factorial = 1.0;
    double alpha_power = 1.0;
    for (int n = 0; n < 6; ++n) {
        if (n > 0) {
            factorial *= static_cast<double>(n);
            alpha_power *= alpha;
        }
        const double expected =
            std::exp(-alpha * alpha / 2.0) * alpha_power / std::sqrt(factorial);
        CHECK(std::abs(actual(n) - snap::Complex(expected, 0.0)) < 1e-11);
    }
}

TEST_CASE("SNAP diagonal applies configured phases and leaves other levels alone") {
    Eigen::VectorXd theta(3);
    theta << 0.23, -0.71, 1.17;

    const snap::Vector diagonal = snap::snap_diagonal(theta, 5);

    for (int n = 0; n < theta.size(); ++n) {
        const snap::Complex expected =
            std::exp(snap::Complex(0.0, theta(n)));
        CHECK(std::abs(diagonal(n) - expected) < matrix_tolerance);
    }
    for (int n = theta.size(); n < diagonal.size(); ++n) {
        CHECK(std::abs(diagonal(n) - snap::Complex(1.0, 0.0)) <
              matrix_tolerance);
    }
}

TEST_CASE("SNAP diagonal accepts all Fock levels and rejects any excess") {
    const Eigen::VectorXd full = Eigen::VectorXd::Zero(4);
    const Eigen::VectorXd excess = Eigen::VectorXd::Zero(5);

    CHECK(snap::snap_diagonal(full, 4).size() == 4);
    CHECK_THROWS_AS(snap::snap_diagonal(excess, 4), std::invalid_argument);
}
