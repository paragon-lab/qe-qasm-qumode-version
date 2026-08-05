#ifdef ECD_WITH_TORCH

#include <catch2/catch_test_macros.hpp>

#include "ecd_parameter_finder.hpp"

namespace {

template <typename Objective>
Eigen::VectorXd finite_difference(const Eigen::VectorXd& params,
                                  Objective&& objective) {
    constexpr double step = 1e-6;
    Eigen::VectorXd gradient(params.size());
    Eigen::VectorXd shifted = params;
    for (int index = 0; index < params.size(); ++index) {
        const double value = shifted(index);
        shifted(index) = value + step;
        const double upper = objective(shifted);
        shifted(index) = value - step;
        const double lower = objective(shifted);
        shifted(index) = value;
        gradient(index) = (upper - lower) / (2.0 * step);
    }
    return gradient;
}

ecd::Matrix test_betas() {
    ecd::Matrix betas(1, 1);
    betas(0, 0) = ecd::Complex(0.37, -0.24);
    return betas;
}

Eigen::MatrixXd test_rotations() {
    Eigen::MatrixXd rotations(2, 2);
    rotations << 0.42, -0.31,
                 0.87, 0.26;
    return rotations;
}

}

TEST_CASE("Torch state gradient matches finite differences") {
    const ecd::Matrix betas = test_betas();
    const Eigen::MatrixXd rotations = test_rotations();
    ecd::Vector target(2);
    target << ecd::Complex(0.8, 0.1), ecd::Complex(-0.2, 0.5);
    target.normalize();
    const Eigen::VectorXd params = ecd::pack_params(betas, rotations);
    auto objective = [&](const Eigen::VectorXd& values) {
        ecd::Matrix shifted_betas;
        Eigen::MatrixXd shifted_rotations;
        ecd::unpack_params(values, 1, 1, shifted_betas, shifted_rotations);
        return ecd::state_objective(shifted_betas, shifted_rotations, target,
                                    2, 1, 0.2);
    };

    const Eigen::VectorXd expected = finite_difference(params, objective);
    const Eigen::VectorXd actual = ecd::torch_objective_gradient(
        betas, rotations, target, 2, 1, 0.2);

    CHECK(actual.isApprox(expected, 1e-5));
}

TEST_CASE("Torch unitary gradient matches finite differences") {
    const ecd::Matrix betas = test_betas();
    const Eigen::MatrixXd rotations = test_rotations();
    const ecd::Matrix target = ecd::rotation_matrix(0.63, -0.18);
    const Eigen::VectorXd params = ecd::pack_params(betas, rotations);
    auto objective = [&](const Eigen::VectorXd& values) {
        ecd::Matrix shifted_betas;
        Eigen::MatrixXd shifted_rotations;
        ecd::unpack_params(values, 1, 1, shifted_betas, shifted_rotations);
        return ecd::unitary_objective(shifted_betas, shifted_rotations, target,
                                      2, 2, 1, 0.2);
    };

    const Eigen::VectorXd expected = finite_difference(params, objective);
    const Eigen::VectorXd actual = ecd::torch_unitary_objective_gradient(
        betas, rotations, target, 2, 2, 1, 0.2);

    CHECK(actual.isApprox(expected, 1e-5));
}

#endif
