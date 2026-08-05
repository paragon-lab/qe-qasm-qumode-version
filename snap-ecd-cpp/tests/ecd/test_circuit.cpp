#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ecd_parameter_finder.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("logical embedding maps multi-mode basis states into Fock space") {
    const ecd::Matrix embedding = ecd::logical_embedding(2, 2, 3);
    ecd::Matrix expected = ecd::Matrix::Zero(9, 4);
    expected(0, 0) = 1.0;
    expected(1, 1) = 1.0;
    expected(3, 2) = 1.0;
    expected(4, 3) = 1.0;

    CHECK(embedding.isApprox(expected, 1e-15));
}

TEST_CASE("zero-depth state circuit returns the vacuum") {
    const ecd::Matrix betas(0, 1);
    const Eigen::MatrixXd rotations = Eigen::MatrixXd::Zero(1, 2);
    const ecd::CircuitResult result = ecd::run_circuit(betas, rotations, 4);
    const ecd::Vector vacuum = ecd::Vector::Unit(4, 0);

    CHECK(result.psi_g.isApprox(vacuum, 1e-15));
    CHECK(result.penalty == 0.0);
    CHECK(result.boundary == 0.0);
}

TEST_CASE("state infidelity allows a subnormalized ground block") {
    const ecd::Vector target = ecd::Vector::Unit(3, 1);
    const ecd::Vector ground_block = 0.4 * target;

    CHECK_THAT(ecd::state_infidelity(target, ground_block),
               WithinAbs(0.6, 1e-15));
}

TEST_CASE("state infidelity rejects an unnormalized target") {
    const ecd::Vector target = 2.0 * ecd::Vector::Unit(3, 1);
    const ecd::Vector ground_block = ecd::Vector::Unit(3, 1);

    CHECK_THROWS_AS(ecd::state_infidelity(target, ground_block),
                    std::invalid_argument);
}

TEST_CASE("zero-depth unitary circuit returns the logical identity") {
    const ecd::Matrix betas(0, 1);
    const Eigen::MatrixXd rotations = Eigen::MatrixXd::Zero(1, 2);
    const ecd::UnitaryCircuitResult result =
        ecd::run_unitary_circuit(betas, rotations, 2, 4);
    const ecd::Matrix embedding = ecd::logical_embedding(2, 1, 4);

    CHECK(result.ground_block.isApprox(embedding, 1e-15));
    CHECK_THAT(ecd::unitary_infidelity(embedding, result.ground_block),
               WithinAbs(0.0, 1e-15));
}

TEST_CASE("one ECD step realizes its positive displacement up to global phase") {
    const double pi = std::acos(-1.0);
    const ecd::Complex beta(0.46, -0.21);
    ecd::Matrix betas(1, 1);
    betas(0, 0) = beta;
    Eigen::MatrixXd rotations(2, 2);
    rotations << 0.0, 0.0,
                 pi, 0.0;

    const ecd::UnitaryCircuitResult result =
        ecd::run_unitary_circuit(betas, rotations, 2, 2);
    const ecd::Matrix target = ecd::displacement(beta / 2.0, 2);

    CHECK(result.ground_block.isApprox(ecd::Complex(0.0, -1.0) * target,
                                       1e-12));
    CHECK_THAT(ecd::unitary_infidelity(target, result.ground_block),
               WithinAbs(0.0, 1e-12));
}

TEST_CASE("one ECD step prepares a known displaced vacuum state") {
    const double pi = std::acos(-1.0);
    const ecd::Complex beta(0.54, -0.18);
    ecd::Matrix betas(1, 1);
    betas(0, 0) = beta;
    Eigen::MatrixXd rotations(2, 2);
    rotations << 0.0, 0.0,
                 pi, 0.0;
    const ecd::CircuitResult result = ecd::run_circuit(betas, rotations, 6);
    const ecd::Vector vacuum = ecd::Vector::Unit(6, 0);
    const ecd::Vector target = ecd::displacement(beta / 2.0, 6) * vacuum;

    CHECK(result.psi_g.isApprox(ecd::Complex(0.0, -1.0) * target, 1e-12));
    CHECK_THAT(ecd::state_infidelity(target, result.psi_g),
               WithinAbs(0.0, 1e-12));
}

TEST_CASE("ECD boundary penalty pins the hard-cutoff weighting") {
    // One ECD layer, one mode, N=3, n_penalize=2. The penalty weights the top
    // two Fock levels by exp((N-n_penalize+i)+1-N): exp(-1) then exp(0)=1.
    const int N = 3;
    const ecd::Complex beta(0.6, 0.0);
    ecd::Matrix betas(1, 1);
    betas(0, 0) = beta;
    Eigen::MatrixXd rotations(2, 2);
    rotations << 0.7, 0.3,
                 0.0, 0.0;  // second rotation runs after the penalty accrues

    // Reconstruct the single accumulated joint state from public primitives.
    const ecd::Matrix a = core::annihilation(N).cast<ecd::Complex>();
    const ecd::Matrix adag = a.adjoint();
    const ecd::Matrix R = ecd::rotation_matrix(0.7, 0.3);
    const auto [D_neg, D_pos] = ecd::ecd(beta, a, adag);
    const ecd::Vector vacuum = ecd::Vector::Unit(N, 0);

    const ecd::Vector excited = R(1, 1) * (D_pos * vacuum);
    const ecd::Vector ground = R(0, 1) * (D_neg * vacuum);
    ecd::Vector joint(2 * N);
    joint.head(N) = excited;
    joint.tail(N) = ground;
    const Eigen::MatrixXd probs = ecd::mode_fock_probs(joint, 1, N);

    const double w_next = std::exp(-1.0);  // level N-2
    const double w_top = 1.0;              // level N-1
    const double expected_penalty = w_next * probs(0, 1) + w_top * probs(0, 2);
    const double expected_boundary = probs(0, 2);

    const ecd::CircuitResult result = ecd::run_circuit(betas, rotations, N, 2);

    CHECK_THAT(result.penalty, WithinAbs(expected_penalty, 1e-12));
    CHECK_THAT(result.boundary, WithinAbs(expected_boundary, 1e-12));
}

TEST_CASE("unitary infidelity ignores global phase") {
    const ecd::Matrix target = ecd::Matrix::Identity(3, 3);
    const ecd::Matrix phased =
        std::exp(ecd::Complex(0.0, 0.67)) * target;

    CHECK_THAT(ecd::unitary_infidelity(target, phased),
               WithinAbs(0.0, 1e-15));
}

TEST_CASE("unitary infidelity rejects a non-isometric target") {
    ecd::Matrix target = ecd::Matrix::Identity(2, 2);
    target(0, 0) = 2.0;

    CHECK_THROWS_AS(ecd::unitary_infidelity(target,
                                             ecd::Matrix::Identity(2, 2)),
                    std::invalid_argument);
}
