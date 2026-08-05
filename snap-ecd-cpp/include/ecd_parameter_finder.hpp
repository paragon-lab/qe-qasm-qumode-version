#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include <Eigen/Dense>

#include "core/fock.hpp"
#include "core/types.hpp"

namespace ecd {

using core::Complex;
using core::Matrix;
using core::Vector;

Matrix displacement(Complex alpha, const Matrix& a, const Matrix& adag);
Matrix displacement(Complex alpha, int N);

std::pair<Matrix, Matrix> ecd(Complex beta, const Matrix& a, const Matrix& adag);

Matrix rotation_matrix(double theta, double phi);

Matrix embed_cavity(const Matrix& op, int j, int num_modes, int N);

struct CircuitResult {
    Vector psi_g;
    double penalty;
    double boundary;
};

struct UnitaryCircuitResult {
    Matrix ground_block;
    double penalty;
    double boundary;
};

CircuitResult run_circuit(const Matrix& betas,
                          const Eigen::MatrixXd& rotations,
                          int N, int n_penalize = 0);

Matrix logical_embedding(int d, int num_modes, int N);

UnitaryCircuitResult run_unitary_circuit(const Matrix& betas,
                                         const Eigen::MatrixXd& rotations,
                                         int d, int N,
                                         int n_penalize = 0);

Eigen::MatrixXd mode_fock_probs(const Vector& psi, int num_modes, int N);
double state_infidelity(const Vector& psi_target, const Vector& psi_g);
double unitary_infidelity(const Matrix& target, const Matrix& ground_block);

double state_objective(const Matrix& betas, const Eigen::MatrixXd& rotations,
                       const Vector& psi_target, int N, int n_penalize,
                       double penalty_weight);

double unitary_objective(const Matrix& betas,
                         const Eigen::MatrixXd& rotations,
                         const Matrix& unitary_target, int d, int N,
                         int n_penalize, double penalty_weight);

#ifdef ECD_WITH_TORCH
Eigen::VectorXd torch_objective_gradient(const Matrix& betas,
                                         const Eigen::MatrixXd& rotations,
                                         const Vector& psi_target, int N,
                                         int n_penalize, double penalty_weight);

Eigen::VectorXd torch_unitary_objective_gradient(
    const Matrix& betas, const Eigen::MatrixXd& rotations,
    const Matrix& unitary_target, int d, int N, int n_penalize,
    double penalty_weight);
#endif

void unpack_params(const Eigen::VectorXd& params, int k, int num_modes,
                   Matrix& betas, Eigen::MatrixXd& rotations);

Eigen::VectorXd pack_params(const Matrix& betas,
                            const Eigen::MatrixXd& rotations);

enum class GradMethod {
    FiniteDifference,
    Autodiff,
};

struct CompiledCircuit {
    Matrix          betas;
    Eigen::MatrixXd rotations;
    double          err = 0.0;
    double          boundary_leakage = 0.0;
    int             restarts_used = 0;
    int             iterations = 0;
    int             objective_evaluations = 0;
    bool            accepted = false;

    int k() const { return static_cast<int>(betas.rows()); }
};

struct ECDOptions {
    int        n_penalize     = 0;
    double     penalty_weight = 0.0;
    double     err_th         = 0.01;
    int        n_restarts     = 20;
    double     lbfgs_tol      = 1e-5;
    int        lbfgs_max_iter = 10000;
    int        lbfgs_max_linesearch = 50;
    uint64_t   seed           = 0;
    GradMethod grad_method    = GradMethod::FiniteDifference;
    double     fd_step        = 1e-6;
    bool       use_initial_guess = false;
    Eigen::VectorXd initial_guess;
    std::function<bool(const CompiledCircuit&)> candidate_acceptor;
};

class ECDParameterFinder {
public:
    ECDParameterFinder(int d, int num_modes, ECDOptions opt = {});

    Vector pad_state(const Vector& target, int N) const;

    double state_infidelity(const Eigen::VectorXd& params,
                            const Vector& psi_target, int k, int N) const;

    double unitary_infidelity(const Eigen::VectorXd& params,
                              const Matrix& unitary_target, int k, int N) const;

    std::optional<CompiledCircuit> find_state_parameters(
        const Vector& target, int k, int N, uint64_t seed) const;

    std::optional<CompiledCircuit> find_unitary_parameters(
        const Matrix& target, int k, int N, uint64_t seed) const;

    CompiledCircuit attempt_state_parameters(
        const Vector& target, int k, int N, uint64_t seed) const;

    CompiledCircuit attempt_unitary_parameters(
        const Matrix& target, int k, int N, uint64_t seed) const;

    std::optional<CompiledCircuit> find_state_parameters_adaptive_k(
        const Vector& target, int k_init, int k_max, int N, int k_step,
        uint64_t seed) const;

    std::optional<CompiledCircuit> find_unitary_parameters_adaptive_k(
        const Matrix& target, int k_init, int k_max, int N, int k_step,
        uint64_t seed) const;

    int d() const { return d_; }
    int num_modes() const { return num_modes_; }

private:
    Eigen::VectorXd state_gradient(const Eigen::VectorXd& params,
                                   const Vector& psi_target, int k, int N) const;

    Eigen::VectorXd unitary_gradient(const Eigen::VectorXd& params,
                                     const Matrix& unitary_target,
                                     int k, int N) const;

    int        d_;
    int        num_modes_;
    ECDOptions opt_;
};

}
