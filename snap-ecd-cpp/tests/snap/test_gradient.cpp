#include <catch2/catch_test_macros.hpp>

#include "snap_parameter_finder.hpp"

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

Eigen::VectorXd test_alphas() {
    Eigen::VectorXd alphas(3);
    alphas << 0.21, -0.34, 0.16;
    return alphas;
}

Eigen::MatrixXd test_thetas() {
    Eigen::MatrixXd thetas(2, 4);
    thetas << 0.17, -0.29, 0.41, -0.53,
              -0.23, 0.37, -0.47, 0.61;
    return thetas;
}

}

TEST_CASE("SNAP flattened gradients use layer-major theta order") {
    snap::Gradients gradients;
    gradients.d_alphas.resize(2);
    gradients.d_alphas << 1.0, 2.0;
    gradients.d_thetas.resize(2, 3);
    gradients.d_thetas << 3.0, 4.0, 5.0,
                          6.0, 7.0, 8.0;
    Eigen::VectorXd expected(8);
    expected << 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0;

    CHECK(gradients.flat() == expected);
}

TEST_CASE("SNAP state gradient matches central differences") {
    constexpr int dimension = 6;
    constexpr int layers = 2;
    constexpr int levels = 4;
    const snap::DisplacementBasis basis(dimension);
    const Eigen::VectorXd alphas = test_alphas();
    const Eigen::MatrixXd thetas = test_thetas();
    snap::Vector target(dimension);
    target << snap::Complex(0.7, 0.1), snap::Complex(-0.2, 0.3),
              snap::Complex(0.1, -0.4), snap::Complex(0.2, 0.1),
              snap::Complex(-0.1, 0.2), snap::Complex(0.05, -0.1);
    target.normalize();
    const Eigen::VectorXd params =
        snap::alphas_and_thetas_to_params(alphas, thetas);
    auto objective = [&](const Eigen::VectorXd& values) {
        return snap::cost_state(target, basis,
                                snap::params_to_alphas(values, layers, levels),
                                snap::params_to_thetas(values, layers, levels));
    };

    const Eigen::VectorXd expected = finite_difference(params, objective);
    const Eigen::VectorXd actual =
        snap::gradient_cost_state(target, basis, alphas, thetas).flat();

    CHECK((actual - expected).cwiseAbs().maxCoeff() < 2e-6);
}

TEST_CASE("SNAP unitary gradient matches central differences") {
    constexpr int dimension = 6;
    constexpr int layers = 2;
    constexpr int levels = 4;
    const snap::DisplacementBasis basis(dimension);
    const Eigen::VectorXd alphas = test_alphas();
    const Eigen::MatrixXd thetas = test_thetas();
    const snap::Matrix target = snap::displacement(0.43, dimension);
    const Eigen::VectorXd params =
        snap::alphas_and_thetas_to_params(alphas, thetas);
    auto objective = [&](const Eigen::VectorXd& values) {
        return snap::cost_unitary(
            target, basis,
            snap::params_to_alphas(values, layers, levels),
            snap::params_to_thetas(values, layers, levels));
    };

    const Eigen::VectorXd expected = finite_difference(params, objective);
    const Eigen::VectorXd actual =
        snap::gradient_cost_unitary(target, basis, alphas, thetas).flat();

    CHECK((actual - expected).cwiseAbs().maxCoeff() < 2e-6);
}

TEST_CASE("SNAP logical-block unitary gradient matches central differences") {
    constexpr int dimension = 6;
    constexpr int logical_dimension = 2;
    constexpr int layers = 2;
    constexpr int levels = 4;
    const snap::DisplacementBasis basis(dimension);
    const Eigen::VectorXd alphas = test_alphas();
    const Eigen::MatrixXd thetas = test_thetas();
    snap::Matrix target = snap::Matrix::Identity(dimension, dimension);
    target.topLeftCorner(logical_dimension, logical_dimension) =
        snap::displacement(0.43, logical_dimension);
    const Eigen::VectorXd params =
        snap::alphas_and_thetas_to_params(alphas, thetas);
    auto objective = [&](const Eigen::VectorXd& values) {
        return snap::cost_unitary(
            target, basis,
            snap::params_to_alphas(values, layers, levels),
            snap::params_to_thetas(values, layers, levels),
            logical_dimension);
    };

    const Eigen::VectorXd expected = finite_difference(params, objective);
    const Eigen::VectorXd actual = snap::gradient_cost_unitary(
        target, basis, alphas, thetas, logical_dimension).flat();

    CHECK((actual - expected).cwiseAbs().maxCoeff() < 2e-6);
}

TEST_CASE("SNAP penalized state gradient matches central differences") {
    constexpr int dimension = 6;
    constexpr int layers = 2;
    constexpr int levels = 4;
    constexpr int n_penalize = 2;
    constexpr double penalty_weight = 0.37;
    const snap::DisplacementBasis basis(dimension);
    const Eigen::VectorXd alphas = test_alphas();
    const Eigen::MatrixXd thetas = test_thetas();
    snap::Vector target(dimension);
    target << snap::Complex(0.7, 0.1), snap::Complex(-0.2, 0.3),
              snap::Complex(0.1, -0.4), snap::Complex(0.2, 0.1),
              snap::Complex(-0.1, 0.2), snap::Complex(0.05, -0.1);
    target.normalize();
    const Eigen::VectorXd params =
        snap::alphas_and_thetas_to_params(alphas, thetas);
    auto objective = [&](const Eigen::VectorXd& values) {
        return snap::objective_state(
            target, basis,
            snap::params_to_alphas(values, layers, levels),
            snap::params_to_thetas(values, layers, levels), n_penalize,
            penalty_weight);
    };

    const Eigen::VectorXd expected = finite_difference(params, objective);
    const Eigen::VectorXd actual = snap::gradient_objective_state(
        target, basis, alphas, thetas, n_penalize, penalty_weight).flat();

    CHECK((actual - expected).cwiseAbs().maxCoeff() < 3e-6);
}

TEST_CASE("SNAP penalized logical-unitary gradient matches central differences") {
    constexpr int dimension = 6;
    constexpr int logical_dimension = 2;
    constexpr int layers = 2;
    constexpr int levels = 4;
    constexpr int n_penalize = 2;
    constexpr double penalty_weight = 0.41;
    const snap::DisplacementBasis basis(dimension);
    const Eigen::VectorXd alphas = test_alphas();
    const Eigen::MatrixXd thetas = test_thetas();
    snap::Matrix target = snap::Matrix::Identity(dimension, dimension);
    target.topLeftCorner(logical_dimension, logical_dimension) =
        snap::displacement(0.43, logical_dimension);
    const Eigen::VectorXd params =
        snap::alphas_and_thetas_to_params(alphas, thetas);
    auto objective = [&](const Eigen::VectorXd& values) {
        return snap::objective_unitary(
            target, basis,
            snap::params_to_alphas(values, layers, levels),
            snap::params_to_thetas(values, layers, levels), n_penalize,
            penalty_weight, logical_dimension);
    };

    const Eigen::VectorXd expected = finite_difference(params, objective);
    const Eigen::VectorXd actual = snap::gradient_objective_unitary(
        target, basis, alphas, thetas, n_penalize, penalty_weight,
        logical_dimension).flat();

    CHECK((actual - expected).cwiseAbs().maxCoeff() < 3e-6);
}

TEST_CASE("zero SNAP penalty preserves the original gradients") {
    const snap::DisplacementBasis basis(6);
    const Eigen::VectorXd alphas = test_alphas();
    const Eigen::MatrixXd thetas = test_thetas();
    const snap::Vector state_target = snap::Vector::Unit(6, 0);
    const snap::Matrix unitary_target = snap::Matrix::Identity(6, 6);

    const snap::Gradients state_original = snap::gradient_cost_state(
        state_target, basis, alphas, thetas);
    const snap::Gradients state_penalized = snap::gradient_objective_state(
        state_target, basis, alphas, thetas, 2, 0.0);
    CHECK(state_original.flat() == state_penalized.flat());

    const snap::Gradients unitary_original = snap::gradient_cost_unitary(
        unitary_target, basis, alphas, thetas, 2);
    const snap::Gradients unitary_penalized =
        snap::gradient_objective_unitary(
            unitary_target, basis, alphas, thetas, 2, 0.0, 2);
    CHECK(unitary_original.flat() == unitary_penalized.flat());
}
