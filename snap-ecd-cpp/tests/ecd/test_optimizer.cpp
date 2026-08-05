#include <catch2/catch_test_macros.hpp>

#include "ecd_parameter_finder.hpp"

namespace {

ecd::ECDOptions quick_options() {
    ecd::ECDOptions options;
    options.err_th = 1e-3;
    options.n_restarts = 1;
    options.lbfgs_max_iter = 400;
    options.seed = 0;
    options.grad_method = ecd::GradMethod::FiniteDifference;
    return options;
}

}

TEST_CASE("ECD state finder prepares the vacuum") {
    const ecd::Vector target = ecd::Vector::Unit(2, 0);
    const ecd::ECDOptions options = quick_options();
    const ecd::ECDParameterFinder finder(2, 1, options);

    const auto circuit = finder.find_state_parameters(target, 0, 2, 3);

    REQUIRE(circuit.has_value());
    CHECK(circuit->err <= options.err_th);
    const ecd::CircuitResult replay =
        ecd::run_circuit(circuit->betas, circuit->rotations, 2);
    CHECK(ecd::state_infidelity(target, replay.psi_g) <= options.err_th);
}

TEST_CASE("ECD unitary finder prepares the identity") {
    const ecd::Matrix target = ecd::Matrix::Identity(2, 2);
    const ecd::ECDOptions options = quick_options();
    const ecd::ECDParameterFinder finder(2, 1, options);

    const auto circuit = finder.find_unitary_parameters(target, 0, 2, 3);

    REQUIRE(circuit.has_value());
    CHECK(circuit->err <= options.err_th);
    const ecd::UnitaryCircuitResult replay =
        ecd::run_unitary_circuit(circuit->betas, circuit->rotations, 2, 2);
    CHECK(ecd::unitary_infidelity(target, replay.ground_block) <= options.err_th);
}

TEST_CASE("ECD finder rejects an incompatible initial guess") {
    ecd::ECDOptions options = quick_options();
    options.use_initial_guess = true;
    options.initial_guess = Eigen::VectorXd::Zero(1);
    const ecd::ECDParameterFinder finder(2, 1, options);
    const ecd::Vector target = ecd::Vector::Unit(2, 0);

    CHECK_THROWS_AS(
        finder.attempt_state_parameters(target, 1, 2, 0),
        std::invalid_argument);
}
