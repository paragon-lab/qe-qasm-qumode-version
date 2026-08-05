#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>

#include <Eigen/Dense>

#include "core/fock.hpp"
#include "core/types.hpp"

namespace snap {

using core::Complex;
using core::Matrix;
using core::Vector;

struct DisplacementBasis {
    explicit DisplacementBasis(int n_dim);

    int    n_dim;
    Vector L;
    Matrix P;
    Matrix Pd;

    Matrix displacement(double alpha) const;

    Vector phase_column(double alpha) const;
};

Matrix displacement(double alpha, int n_dim);

Vector snap_diagonal(const Eigen::VectorXd& theta, int n_dim);

Matrix ansatz_unitary(const DisplacementBasis& basis,
                      const Eigen::VectorXd& alphas,
                      const Eigen::MatrixXd& thetas);

Vector ansatz_state(const DisplacementBasis& basis,
                    const Eigen::VectorXd& alphas,
                    const Eigen::MatrixXd& thetas);

double cost_unitary(const Matrix& U_target,
                    const DisplacementBasis& basis,
                    const Eigen::VectorXd& alphas,
                    const Eigen::MatrixXd& thetas,
                    std::optional<int> d_fid = std::nullopt);

double cost_state(const Vector& psi_target,
                  const DisplacementBasis& basis,
                  const Eigen::VectorXd& alphas,
                  const Eigen::MatrixXd& thetas);

struct BoundaryMetrics {
    double penalty = 0.0;
    double leakage = 0.0;
};

BoundaryMetrics boundary_metrics_unitary(
    const DisplacementBasis& basis, const Eigen::VectorXd& alphas,
    const Eigen::MatrixXd& thetas, int n_penalize,
    std::optional<int> d_fid = std::nullopt);

BoundaryMetrics boundary_metrics_state(
    const DisplacementBasis& basis, const Eigen::VectorXd& alphas,
    const Eigen::MatrixXd& thetas, int n_penalize);

double objective_unitary(const Matrix& U_target,
                         const DisplacementBasis& basis,
                         const Eigen::VectorXd& alphas,
                         const Eigen::MatrixXd& thetas,
                         int n_penalize, double penalty_weight,
                         std::optional<int> d_fid = std::nullopt);

double objective_state(const Vector& psi_target,
                       const DisplacementBasis& basis,
                       const Eigen::VectorXd& alphas,
                       const Eigen::MatrixXd& thetas,
                       int n_penalize, double penalty_weight);

struct Gradients {
    Eigen::VectorXd d_alphas;
    Eigen::MatrixXd d_thetas;

    Eigen::VectorXd flat() const;
};

Gradients gradient_cost_unitary(const Matrix& U_target,
                                const DisplacementBasis& basis,
                                const Eigen::VectorXd& alphas,
                                const Eigen::MatrixXd& thetas,
                                std::optional<int> d_fid = std::nullopt);

Gradients gradient_cost_state(const Vector& psi_target,
                              const DisplacementBasis& basis,
                              const Eigen::VectorXd& alphas,
                              const Eigen::MatrixXd& thetas);

Gradients gradient_objective_unitary(
    const Matrix& U_target, const DisplacementBasis& basis,
    const Eigen::VectorXd& alphas, const Eigen::MatrixXd& thetas,
    int n_penalize, double penalty_weight,
    std::optional<int> d_fid = std::nullopt);

Gradients gradient_objective_state(
    const Vector& psi_target, const DisplacementBasis& basis,
    const Eigen::VectorXd& alphas, const Eigen::MatrixXd& thetas,
    int n_penalize, double penalty_weight);

inline Eigen::VectorXd params_to_alphas(const Eigen::VectorXd& params, int k,
                                        int n_levels) {
    if (params.size() != k + 1 + k * n_levels) {
        throw std::invalid_argument("params length does not match k/n_levels.");
    }
    return params.head(k + 1);
}

inline Eigen::MatrixXd params_to_thetas(const Eigen::VectorXd& params, int k,
                                        int n_levels) {
    if (params.size() != k + 1 + k * n_levels) {
        throw std::invalid_argument("params length does not match k/n_levels.");
    }
    Eigen::MatrixXd thetas(k, n_levels);
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < n_levels; ++j)
            thetas(i, j) = params(k + 1 + i * n_levels + j);
    return thetas;
}

inline Eigen::VectorXd alphas_and_thetas_to_params(const Eigen::VectorXd& alphas,
                                                   const Eigen::MatrixXd& thetas) {
    const int k = static_cast<int>(thetas.rows());
    const int n_levels = static_cast<int>(thetas.cols());
    if (alphas.size() != k + 1) {
        throw std::invalid_argument("alphas length should be thetas.rows()+1.");
    }
    Eigen::VectorXd params(k + 1 + k * n_levels);
    params.head(k + 1) = alphas;
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < n_levels; ++j)
            params(k + 1 + i * n_levels + j) = thetas(i, j);
    return params;
}

struct PulseResult {
    Eigen::VectorXd params;
    double          err = 0.0;
    int             runs_used = 0;
    bool            converged = false;
    int             iterations = 0;
    int             objective_evaluations = 0;
    double          boundary_leakage = 0.0;

    Eigen::VectorXd alphas(int k, int n_levels) const;
    Eigen::MatrixXd thetas(int k, int n_levels) const;
};

struct SnapOptions {
    int      n_levels       = -1;
    int      max_runs       = 50;
    double   err_th         = 0.01;
    int      d_fid          = -1;
    double   lbfgs_tol      = 1e-5;
    int      lbfgs_max_iter = 10000;
    int      lbfgs_max_linesearch = 50;
    uint64_t seed           = 0;
    bool     use_initial_guess = false;
    Eigen::VectorXd initial_guess;
    int      n_penalize = 0;
    double   penalty_weight = 0.0;
    std::function<bool(const PulseResult&)> candidate_acceptor;
};

PulseResult pulse_parameter_finder_unitary(const Matrix& U_target, int k,
                                           int n_dim,
                                           const SnapOptions& opt = {});

PulseResult pulse_parameter_finder_state(const Vector& psi_target, int k,
                                         int n_dim,
                                         const SnapOptions& opt = {});

}
