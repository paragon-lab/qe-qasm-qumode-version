#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "core/fock.hpp"

TEST_CASE("annihilation constructs the truncated ladder operator") {
    Eigen::MatrixXd expected = Eigen::MatrixXd::Zero(4, 4);
    expected(0, 1) = 1.0;
    expected(1, 2) = std::sqrt(2.0);
    expected(2, 3) = std::sqrt(3.0);

    CHECK(core::annihilation(4).isApprox(expected, 1e-15));
}
