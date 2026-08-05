#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

#include <Eigen/Dense>

#include "core/types.hpp"

namespace decomp {

using core::Matrix;
using core::Vector;

enum class GateSet {
    Snap,
    Ecd,
};

enum class GradientMethod {
    Automatic,
    Autodiff,
    FiniteDifference,
};

struct ECDOptions {
    int            n_penalize = -1;
    double         penalty_weight = 0.1;
    GradientMethod gradient_method = GradientMethod::Automatic;
    double         finite_difference_step = 1e-6;
    std::optional<int> warm_start_layers;
};

struct SnapOptions {
    int    n_penalize = -1;
    double penalty_weight = 0.1;
};

struct Options {
    std::optional<int> layers;
    std::optional<int> buffers;
    int                max_layers = 0;
    int                max_buffers = 10;
    int                replay_increment = 12;

    int      max_restarts = 50;
    double   optimization_threshold = 1e-3;
    double   stability_threshold = 1e-2;
    double   lbfgs_tolerance = 1e-5;
    int      lbfgs_max_iterations = 10000;
    int      lbfgs_max_linesearch = 50;
    uint64_t seed = 0;

    ECDOptions ecd;
    SnapOptions snap;
};

struct SnapCircuit {
    Eigen::VectorXd alphas;
    Eigen::MatrixXd thetas;
};

struct ECDCircuit {
    Matrix          betas;
    Eigen::MatrixXd rotations;
};

using Circuit = std::variant<SnapCircuit, ECDCircuit>;

struct ReplayCheck {
    int    cutoff_per_mode = 0;
    double error = std::numeric_limits<double>::quiet_NaN();
};

struct SearchAttempt {
    int layers = 0;
    int buffers = 0;
};

class DecompositionError : public std::runtime_error {
public:
    DecompositionError(GateSet gate_set,
                       std::vector<SearchAttempt> attempts,
                       double best_error, double best_replay_error,
                       bool had_training_convergence);

    GateSet gate_set() const noexcept { return gate_set_; }
    const std::vector<SearchAttempt>& attempts() const noexcept {
        return attempts_;
    }
    double best_error() const noexcept { return best_error_; }
    double best_replay_error() const noexcept { return best_replay_error_; }
    bool had_training_convergence() const noexcept {
        return had_training_convergence_;
    }

private:
    GateSet                    gate_set_;
    std::vector<SearchAttempt> attempts_;
    double                     best_error_;
    double                     best_replay_error_;
    bool                       had_training_convergence_;
};

struct Result {
    GateSet gate_set = GateSet::Snap;
    Circuit circuit;
    double                   error = 0.0;
    double                   replay_error =
        std::numeric_limits<double>::quiet_NaN();
    std::vector<ReplayCheck> replay_checks;
    bool                     converged = false;
    int                      layers = 0;
    int                      buffers = 0;
    int                      cutoff_per_mode = 0;
    int                      restarts_used = 0;
    int                      iterations = 0;
    int                      objective_evaluations = 0;
    double                   boundary_leakage =
        std::numeric_limits<double>::quiet_NaN();
    double runtime_seconds = 0.0;
};

Result decompose(const Matrix& target, int modes, GateSet gate_set,
                 const Options& options = {});

Result decompose(const Vector& target, int modes, GateSet gate_set,
                 const Options& options = {});

template <typename Derived>
Result decompose(const Eigen::MatrixBase<Derived>& target, int modes,
                 GateSet gate_set, const Options& options = {}) {
    if constexpr (Derived::ColsAtCompileTime == 1) {
        const Vector value = target;
        return decompose(value, modes, gate_set, options);
    } else {
        const Matrix value = target;
        return decompose(value, modes, gate_set, options);
    }
}

}
