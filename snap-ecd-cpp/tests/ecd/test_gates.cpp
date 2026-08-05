#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "ecd_parameter_finder.hpp"

namespace {

constexpr double tolerance = 1e-12;

bool approximately_equal(const ecd::Matrix& actual,
                         const ecd::Matrix& expected) {
    return actual.isApprox(expected, tolerance);
}

}

TEST_CASE("ECD displacement at zero is identity") {
    const ecd::Matrix actual = ecd::displacement(0.0, 4);
    const ecd::Matrix expected = ecd::Matrix::Identity(4, 4);

    CHECK(approximately_equal(actual, expected));
}

TEST_CASE("two-level real displacement has the expected closed form") {
    const double alpha = 0.37;
    ecd::Matrix expected(2, 2);
    expected << std::cos(alpha), -std::sin(alpha),
                std::sin(alpha),  std::cos(alpha);

    CHECK(approximately_equal(ecd::displacement(alpha, 2), expected));
}

TEST_CASE("ECD complex displacement is unitary") {
    const ecd::Matrix displacement =
        ecd::displacement(ecd::Complex(0.41, -0.28), 5);
    const ecd::Matrix identity = ecd::Matrix::Identity(5, 5);

    CHECK(approximately_equal(displacement.adjoint() * displacement, identity));
}

TEST_CASE("ECD displacement inverse uses the negative amplitude") {
    const ecd::Complex alpha(0.41, -0.28);

    CHECK(approximately_equal(ecd::displacement(alpha, 5).adjoint(),
                              ecd::displacement(-alpha, 5)));
}

TEST_CASE("ECD gate returns negative and positive half displacements") {
    const int dimension = 5;
    const ecd::Complex beta(0.52, -0.31);
    const ecd::Matrix a = core::annihilation(dimension).cast<ecd::Complex>();
    const ecd::Matrix adag = a.adjoint();
    const auto [negative, positive] = ecd::ecd(beta, a, adag);

    CHECK(approximately_equal(negative,
                              ecd::displacement(-beta / 2.0, a, adag)));
    CHECK(approximately_equal(positive,
                              ecd::displacement(beta / 2.0, a, adag)));
    CHECK(approximately_equal(negative, positive.adjoint()));
}

TEST_CASE("zero-angle ECD rotation is identity") {
    const ecd::Matrix identity = ecd::Matrix::Identity(2, 2);

    CHECK(approximately_equal(ecd::rotation_matrix(0.0, 1.23), identity));
}

TEST_CASE("ECD rotation is unitary") {
    const ecd::Matrix rotation = ecd::rotation_matrix(0.83, -0.47);
    const ecd::Matrix identity = ecd::Matrix::Identity(2, 2);

    CHECK(approximately_equal(rotation.adjoint() * rotation, identity));
}

TEST_CASE("pi ECD rotations follow the X and Y phase conventions") {
    const double pi = std::acos(-1.0);
    ecd::Matrix negative_i_x(2, 2);
    negative_i_x << 0.0, ecd::Complex(0.0, -1.0),
                    ecd::Complex(0.0, -1.0), 0.0;
    ecd::Matrix negative_i_y(2, 2);
    negative_i_y << 0.0, -1.0,
                    1.0,  0.0;

    CHECK(approximately_equal(ecd::rotation_matrix(pi, 0.0), negative_i_x));
    CHECK(approximately_equal(ecd::rotation_matrix(pi, pi / 2.0), negative_i_y));
}

TEST_CASE("embed_cavity acts on the requested mode") {
    ecd::Matrix swap(2, 2);
    swap << 0.0, 1.0,
            1.0, 0.0;
    const ecd::Matrix on_mode_zero = ecd::embed_cavity(swap, 0, 2, 2);
    const ecd::Matrix on_mode_one = ecd::embed_cavity(swap, 1, 2, 2);
    const ecd::Vector state_00 = ecd::Vector::Unit(4, 0);
    const ecd::Vector state_10 = ecd::Vector::Unit(4, 2);
    const ecd::Vector state_01 = ecd::Vector::Unit(4, 1);

    CHECK((on_mode_zero * state_00).isApprox(state_10, tolerance));
    CHECK((on_mode_one * state_00).isApprox(state_01, tolerance));
}
