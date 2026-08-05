#include "snap_parameter_finder.hpp"

#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include <LBFGS.h>

namespace snap {

DisplacementBasis::DisplacementBasis(int n_dim_) : n_dim(n_dim_) {
    const Eigen::MatrixXd a = core::annihilation(n_dim);
    const Eigen::MatrixXd A = a.transpose() - a;

    // A is real skew-symmetric, so iA is Hermitian: diagonalise that with the
    // self-adjoint solver (real eigenvalues mu, unitary P) and recover A's own
    // eigenvalues as L = -i*mu, which are purely imaginary.
    const Matrix H = Complex(0.0, 1.0) * A.cast<Complex>();
    Eigen::SelfAdjointEigenSolver<Matrix> es(H);
    if (es.info() != Eigen::Success) {
        throw std::runtime_error("eigendecomposition of (a^dag - a) failed");
    }

    P  = es.eigenvectors();
    Pd = P.adjoint();
    L  = Complex(0.0, -1.0) * es.eigenvalues().cast<Complex>();
}

Vector DisplacementBasis::phase_column(double alpha) const {
    return (alpha * L.array()).exp();
}

Matrix DisplacementBasis::displacement(double alpha) const {
    return P * phase_column(alpha).asDiagonal() * Pd;
}

Matrix displacement(double alpha, int n_dim) {
    return DisplacementBasis(n_dim).displacement(alpha);
}

Vector snap_diagonal(const Eigen::VectorXd& theta, int n_dim) {
    const auto n_levels = theta.size();
    if (n_levels > n_dim) {
        throw std::invalid_argument("theta_vector too long.");
    }
    Vector d = Vector::Ones(n_dim);
    for (Eigen::Index j = 0; j < n_levels; ++j) {
        d(j) = std::exp(Complex(0.0, theta(j)));
    }
    return d;
}

namespace {

void check_shapes(const DisplacementBasis& basis, const Eigen::VectorXd& alphas,
                  const Eigen::MatrixXd& thetas) {
    if (alphas.size() != thetas.rows() + 1) {
        throw std::invalid_argument(
            "Length of alpha array should equal one plus number of theta rows.");
    }
    if (thetas.cols() > basis.n_dim) {
        throw std::invalid_argument("Check theta subarray lengths.");
    }
}

}

Matrix ansatz_unitary(const DisplacementBasis& basis,
                      const Eigen::VectorXd& alphas,
                      const Eigen::MatrixXd& thetas) {
    const Eigen::Index k = thetas.rows();
    check_shapes(basis, alphas, thetas);

    Matrix U = basis.displacement(alphas(0));
    for (Eigen::Index i = 0; i < k; ++i) {
        U = snap_diagonal(thetas.row(i).transpose(), basis.n_dim).asDiagonal() * U;
        U = basis.displacement(alphas(i + 1)) * U;
    }
    return U;
}

Vector ansatz_state(const DisplacementBasis& basis,
                    const Eigen::VectorXd& alphas,
                    const Eigen::MatrixXd& thetas) {
    check_shapes(basis, alphas, thetas);
    const Eigen::Index k = thetas.rows();

    Vector state = Vector::Zero(basis.n_dim);
    state(0) = 1.0;

    // D(alpha) v = P (exp(alpha L) elementwise* (P† v)): two mat-vecs, no gate.
    auto displace = [&](double alpha, const Vector& v) -> Vector {
        return basis.P * basis.phase_column(alpha).cwiseProduct(basis.Pd * v);
    };

    state = displace(alphas(0), state);
    for (Eigen::Index i = 0; i < k; ++i) {
        state = snap_diagonal(thetas.row(i).transpose(), basis.n_dim)
                    .cwiseProduct(state);
        state = displace(alphas(i + 1), state);
    }
    return state;
}

double cost_unitary(const Matrix& U_target, const DisplacementBasis& basis,
                    const Eigen::VectorXd& alphas, const Eigen::MatrixXd& thetas,
                    std::optional<int> d_fid) {
    if (U_target.rows() != basis.n_dim || U_target.cols() != basis.n_dim) {
        throw std::invalid_argument("U_target size incompatible.");
    }
    const Matrix U = ansatz_unitary(basis, alphas, thetas);

    int n = basis.n_dim;
    if (d_fid.has_value()) {
        if (*d_fid <= 0 || *d_fid > n) {
            throw std::invalid_argument("d_fid must be between one and n_dim.");
        }
        n = *d_fid;
    }

    // tr(A†B) as a Frobenius sum, avoiding the full matrix product.
    const Complex c = U_target.topLeftCorner(n, n)
                          .conjugate()
                          .cwiseProduct(U.topLeftCorner(n, n))
                          .sum() /
                      static_cast<double>(n);
    return 1.0 - std::abs(c);
}

double cost_state(const Vector& psi_target, const DisplacementBasis& basis,
                  const Eigen::VectorXd& alphas, const Eigen::MatrixXd& thetas) {
    if (psi_target.size() != basis.n_dim) {
        throw std::invalid_argument("Target state size incompatible.");
    }
    if (std::abs(psi_target.squaredNorm() - 1.0) > 1e-9) {
        throw std::invalid_argument("State psi is not normalised.");
    }
    const Vector state = ansatz_state(basis, alphas, thetas);
    return 1.0 - std::abs(psi_target.dot(state));
}

namespace {

using RowVector = Eigen::RowVectorXcd;

void validate_boundary_options(int n_penalize, double penalty_weight,
                               int dimension) {
    if (n_penalize < 0 || n_penalize > dimension) {
        throw std::invalid_argument(
            "n_penalize must be between zero and the SNAP dimension");
    }
    if (!std::isfinite(penalty_weight) || penalty_weight < 0.0) {
        throw std::invalid_argument(
            "penalty_weight must be finite and nonnegative");
    }
}

Eigen::VectorXd boundary_weights(int dimension, int n_penalize) {
    Eigen::VectorXd weights = Eigen::VectorXd::Zero(dimension);
    for (int level = dimension - n_penalize; level < dimension; ++level) {
        weights(level) = std::exp(level + 1.0 - dimension);
    }
    return weights;
}

struct BoundaryEvolution {
    std::vector<Matrix> displaced;
    std::vector<Matrix> phased;
    BoundaryMetrics metrics;
};

BoundaryEvolution evolve_boundaries(const DisplacementBasis& basis,
                                    const Eigen::VectorXd& alphas,
                                    const Eigen::MatrixXd& thetas,
                                    const Matrix& initial,
                                    int n_penalize) {
    check_shapes(basis, alphas, thetas);
    validate_boundary_options(n_penalize, 0.0, basis.n_dim);
    if (initial.rows() != basis.n_dim || initial.cols() == 0) {
        throw std::invalid_argument(
            "initial SNAP states have incompatible dimensions");
    }

    const Eigen::Index k = thetas.rows();
    const Eigen::VectorXd weights =
        boundary_weights(basis.n_dim, n_penalize);
    BoundaryEvolution result;
    result.displaced.reserve(static_cast<std::size_t>(k + 1));
    result.phased.reserve(static_cast<std::size_t>(k));

    Matrix current = basis.displacement(alphas(0)) * initial;
    result.displaced.push_back(current);
    for (Eigen::Index layer = 0; layer < k; ++layer) {
        current = snap_diagonal(thetas.row(layer).transpose(), basis.n_dim)
                      .asDiagonal() *
                  current;
        result.phased.push_back(current);
        current = basis.displacement(alphas(layer + 1)) * current;
        result.displaced.push_back(current);
    }

    const double column_count = static_cast<double>(initial.cols());
    for (const Matrix& states : result.displaced) {
        result.metrics.leakage = std::max(
            result.metrics.leakage,
            states.row(basis.n_dim - 1).cwiseAbs2().maxCoeff());
        if (n_penalize > 0) {
            result.metrics.penalty +=
                (states.cwiseAbs2().array().colwise() * weights.array())
                    .sum() /
                column_count;
        }
    }
    return result;
}

Gradients boundary_gradients(const DisplacementBasis& basis,
                             const Eigen::VectorXd& alphas,
                             const Eigen::MatrixXd& thetas,
                             const Matrix& initial, int n_penalize) {
    const BoundaryEvolution evolution =
        evolve_boundaries(basis, alphas, thetas, initial, n_penalize);
    const Eigen::Index k = thetas.rows();
    const Eigen::Index n_levels = thetas.cols();
    Gradients result{Eigen::VectorXd::Zero(k + 1),
                     Eigen::MatrixXd::Zero(k, n_levels)};
    if (n_penalize == 0) return result;

    const Eigen::VectorXd weights =
        boundary_weights(basis.n_dim, n_penalize);
    const Matrix weighted = weights.cast<Complex>().asDiagonal();
    const Matrix generator =
        (core::annihilation(basis.n_dim).transpose() -
         core::annihilation(basis.n_dim))
            .cast<Complex>();
    const double scale = 2.0 / static_cast<double>(initial.cols());
    Matrix adjoint = Matrix::Zero(initial.rows(), initial.cols());

    for (Eigen::Index boundary = k; boundary >= 0; --boundary) {
        const Matrix& state =
            evolution.displaced[static_cast<std::size_t>(boundary)];
        adjoint += scale * weighted * state;
        result.d_alphas(boundary) =
            (adjoint.conjugate().cwiseProduct(generator * state))
                .sum()
                .real();

        Matrix before_displacement =
            basis.displacement(-alphas(boundary)) * adjoint;
        if (boundary > 0) {
            const Eigen::Index layer = boundary - 1;
            const Matrix& phased =
                evolution.phased[static_cast<std::size_t>(layer)];
            for (Eigen::Index level = 0; level < n_levels; ++level) {
                result.d_thetas(layer, level) =
                    (before_displacement.row(level)
                         .conjugate()
                         .cwiseProduct(Complex(0.0, 1.0) *
                                       phased.row(level)))
                        .sum()
                        .real();
            }
            const Vector inverse_phases =
                snap_diagonal((-thetas.row(layer)).transpose(), basis.n_dim);
            adjoint = inverse_phases.asDiagonal() * before_displacement;
        }
    }
    return result;
}

Matrix logical_columns(int dimension, int logical_dimension) {
    if (logical_dimension <= 0 || logical_dimension > dimension) {
        throw std::invalid_argument(
            "d_fid must be between one and the SNAP dimension");
    }
    Matrix initial = Matrix::Zero(dimension, logical_dimension);
    initial.topRows(logical_dimension) =
        Matrix::Identity(logical_dimension, logical_dimension);
    return initial;
}

Complex trace_of_product(const Matrix& X, const Matrix& Y) {
    return X.cwiseProduct(Y.transpose()).sum();
}

// Turn the raw complex trace-derivatives g into the real gradient of 1 - |c|:
// d(1-|c|)/dp = -Re(conj(c) g) / (norm |c|).
template <typename Derived>
auto rescale(const Eigen::MatrixBase<Derived>& g, Complex c, double norm) {
    const double scale = -1.0 / (norm * std::abs(c));
    return (scale * (std::conj(c) * g.array()).real()).matrix().eval();
}

}

BoundaryMetrics boundary_metrics_unitary(
    const DisplacementBasis& basis, const Eigen::VectorXd& alphas,
    const Eigen::MatrixXd& thetas, int n_penalize,
    std::optional<int> d_fid) {
    const int logical_dimension = d_fid.value_or(basis.n_dim);
    return evolve_boundaries(
               basis, alphas, thetas,
               logical_columns(basis.n_dim, logical_dimension), n_penalize)
        .metrics;
}

BoundaryMetrics boundary_metrics_state(
    const DisplacementBasis& basis, const Eigen::VectorXd& alphas,
    const Eigen::MatrixXd& thetas, int n_penalize) {
    Matrix vacuum = Matrix::Zero(basis.n_dim, 1);
    vacuum(0, 0) = 1.0;
    return evolve_boundaries(basis, alphas, thetas, vacuum, n_penalize)
        .metrics;
}

double objective_unitary(const Matrix& U_target,
                         const DisplacementBasis& basis,
                         const Eigen::VectorXd& alphas,
                         const Eigen::MatrixXd& thetas,
                         int n_penalize, double penalty_weight,
                         std::optional<int> d_fid) {
    validate_boundary_options(n_penalize, penalty_weight, basis.n_dim);
    return cost_unitary(U_target, basis, alphas, thetas, d_fid) +
           penalty_weight *
               boundary_metrics_unitary(basis, alphas, thetas, n_penalize,
                                        d_fid)
                   .penalty;
}

double objective_state(const Vector& psi_target,
                       const DisplacementBasis& basis,
                       const Eigen::VectorXd& alphas,
                       const Eigen::MatrixXd& thetas,
                       int n_penalize, double penalty_weight) {
    validate_boundary_options(n_penalize, penalty_weight, basis.n_dim);
    return cost_state(psi_target, basis, alphas, thetas) +
           penalty_weight *
               boundary_metrics_state(basis, alphas, thetas, n_penalize)
                   .penalty;
}

Eigen::VectorXd Gradients::flat() const {
    Eigen::VectorXd out(d_alphas.size() + d_thetas.size());
    out.head(d_alphas.size()) = d_alphas;
    Eigen::Index pos = d_alphas.size();
    for (Eigen::Index i = 0; i < d_thetas.rows(); ++i) {
        out.segment(pos, d_thetas.cols()) = d_thetas.row(i);
        pos += d_thetas.cols();
    }
    return out;
}

Gradients gradient_cost_unitary(const Matrix& U_target,
                                const DisplacementBasis& basis,
                                const Eigen::VectorXd& alphas,
                                const Eigen::MatrixXd& thetas,
                                std::optional<int> d_fid) {
    const int n = basis.n_dim;
    if (U_target.rows() != n || U_target.cols() != n) {
        throw std::invalid_argument("U_target size incompatible.");
    }
    const Eigen::Index k = thetas.rows();
    const Eigen::Index n_levels = thetas.cols();
    int fidelity_dimension = n;
    if (d_fid.has_value()) {
        if (*d_fid <= 0 || *d_fid > n) {
            throw std::invalid_argument("d_fid must be between one and n_dim.");
        }
        fidelity_dimension = *d_fid;
    }

    const Eigen::MatrixXd a = core::annihilation(n);
    const Matrix G = (a.transpose() - a).cast<Complex>();

    const Matrix U = ansatz_unitary(basis, alphas, thetas);
    Matrix fidelity_target = Matrix::Zero(n, n);
    fidelity_target.topLeftCorner(fidelity_dimension, fidelity_dimension) =
        U_target.topLeftCorner(fidelity_dimension, fidelity_dimension);
    const Complex c =
        trace_of_product(fidelity_target.adjoint(), U) /
        static_cast<double>(fidelity_dimension);

    Matrix A = fidelity_target.adjoint();
    Matrix B = U;

    Eigen::VectorXcd g_alpha(k + 1);
    Eigen::MatrixXcd g_theta(k, n_levels);

    for (Eigen::Index i = 0; i < k; ++i) {
        g_alpha(i) = trace_of_product(A, B * G);

        // Strip layer i off B's right end, then fold its displacement into A.
        B = B * basis.displacement(-alphas(i));
        B = B * snap_diagonal((-thetas.row(i)).transpose(), n).asDiagonal();
        const Matrix DA = basis.displacement(alphas(i)) * A;

        // dc/dtheta only touches the diagonal of DA*B; take the first n_levels
        // entries as row-wise dot products rather than a full product.
        const Vector diag = DA.topRows(n_levels)
                                .cwiseProduct(B.leftCols(n_levels).transpose())
                                .rowwise()
                                .sum();
        for (Eigen::Index h = 0; h < n_levels; ++h) {
            g_theta(i, h) =
                Complex(0.0, 1.0) * std::exp(Complex(0.0, thetas(i, h))) * diag(h);
        }

        A = snap_diagonal(thetas.row(i).transpose(), n).asDiagonal() * DA;
    }
    g_alpha(k) = trace_of_product(A, B * G);

    return {rescale(g_alpha, c, static_cast<double>(fidelity_dimension)),
            rescale(g_theta, c, static_cast<double>(fidelity_dimension))};
}

Gradients gradient_cost_state(const Vector& psi_target,
                              const DisplacementBasis& basis,
                              const Eigen::VectorXd& alphas,
                              const Eigen::MatrixXd& thetas) {
    const int n = basis.n_dim;
    if (psi_target.size() != n) {
        throw std::invalid_argument("Target state size incompatible.");
    }
    if (std::abs(psi_target.squaredNorm() - 1.0) > 1e-9) {
        throw std::invalid_argument("State psi is not normalised.");
    }
    const Eigen::Index k = thetas.rows();
    const Eigen::Index n_levels = thetas.cols();

    const Eigen::MatrixXd a = core::annihilation(n);
    const Matrix G = (a.transpose() - a).cast<Complex>();

    Vector ket = ansatz_state(basis, alphas, thetas);
    const Complex c = psi_target.dot(ket);

    RowVector bra = psi_target.adjoint();

    auto displace_ket = [&](double alpha, const Vector& v) -> Vector {
        return basis.P * basis.phase_column(alpha).cwiseProduct(basis.Pd * v);
    };
    auto displace_bra = [&](const RowVector& w, double alpha) -> RowVector {
        return (w * basis.P)
                   .cwiseProduct(basis.phase_column(alpha).transpose()) *
               basis.Pd;
    };

    Eigen::VectorXcd g_alpha(k + 1);
    Eigen::MatrixXcd g_theta(k, n_levels);

    // Walk layer k -> 0: the bra absorbs each gate from the left as it is
    // peeled off the ket, so <bra|G|ket> is the displacement gradient at the
    // current boundary.
    g_alpha(k) = (bra * (G * ket)).value();
    bra = displace_bra(bra, alphas(k));
    ket = displace_ket(-alphas(k), ket);

    for (Eigen::Index i = k - 1; i >= 0; --i) {
        ket = snap_diagonal((-thetas.row(i)).transpose(), n).cwiseProduct(ket);
        for (Eigen::Index h = 0; h < n_levels; ++h) {
            g_theta(i, h) = Complex(0.0, 1.0) *
                            std::exp(Complex(0.0, thetas(i, h))) * bra(h) *
                            ket(h);
        }
        bra = bra.cwiseProduct(
            snap_diagonal(thetas.row(i).transpose(), n).transpose());
        g_alpha(i) = (bra * (G * ket)).value();

        ket = displace_ket(-alphas(i), ket);
        bra = displace_bra(bra, alphas(i));
    }

    return {rescale(g_alpha, c, 1.0), rescale(g_theta, c, 1.0)};
}

Gradients gradient_objective_unitary(
    const Matrix& U_target, const DisplacementBasis& basis,
    const Eigen::VectorXd& alphas, const Eigen::MatrixXd& thetas,
    int n_penalize, double penalty_weight,
    std::optional<int> d_fid) {
    validate_boundary_options(n_penalize, penalty_weight, basis.n_dim);
    Gradients result =
        gradient_cost_unitary(U_target, basis, alphas, thetas, d_fid);
    if (penalty_weight == 0.0 || n_penalize == 0) return result;

    const int logical_dimension = d_fid.value_or(basis.n_dim);
    const Gradients penalty = boundary_gradients(
        basis, alphas, thetas,
        logical_columns(basis.n_dim, logical_dimension), n_penalize);
    result.d_alphas += penalty_weight * penalty.d_alphas;
    result.d_thetas += penalty_weight * penalty.d_thetas;
    return result;
}

Gradients gradient_objective_state(
    const Vector& psi_target, const DisplacementBasis& basis,
    const Eigen::VectorXd& alphas, const Eigen::MatrixXd& thetas,
    int n_penalize, double penalty_weight) {
    validate_boundary_options(n_penalize, penalty_weight, basis.n_dim);
    Gradients result =
        gradient_cost_state(psi_target, basis, alphas, thetas);
    if (penalty_weight == 0.0 || n_penalize == 0) return result;

    Matrix vacuum = Matrix::Zero(basis.n_dim, 1);
    vacuum(0, 0) = 1.0;
    const Gradients penalty =
        boundary_gradients(basis, alphas, thetas, vacuum, n_penalize);
    result.d_alphas += penalty_weight * penalty.d_alphas;
    result.d_thetas += penalty_weight * penalty.d_thetas;
    return result;
}

Eigen::VectorXd PulseResult::alphas(int k, int n_levels) const {
    return params_to_alphas(params, k, n_levels);
}
Eigen::MatrixXd PulseResult::thetas(int k, int n_levels) const {
    return params_to_thetas(params, k, n_levels);
}

namespace {

// LBFGSpp calls operator()(x, grad) -> cost. 
struct UnitaryObjective {
    const Matrix& U_target;
    const DisplacementBasis& basis;
    int k, n_levels, d_fid;
    int n_penalize;
    double penalty_weight;

    double operator()(const Eigen::VectorXd& x, Eigen::VectorXd& grad) {
        const Eigen::VectorXd a = params_to_alphas(x, k, n_levels);
        const Eigen::MatrixXd t = params_to_thetas(x, k, n_levels);
        std::optional<int> df =
            d_fid > 0 ? std::optional<int>(d_fid) : std::nullopt;
        grad = gradient_objective_unitary(
                   U_target, basis, a, t, n_penalize, penalty_weight, df)
                   .flat();
        return objective_unitary(U_target, basis, a, t, n_penalize,
                                 penalty_weight, df);
    }
};

struct StateObjective {
    const Vector& psi_target;
    const DisplacementBasis& basis;
    int k, n_levels;
    int n_penalize;
    double penalty_weight;

    double operator()(const Eigen::VectorXd& x, Eigen::VectorXd& grad) {
        const Eigen::VectorXd a = params_to_alphas(x, k, n_levels);
        const Eigen::MatrixXd t = params_to_thetas(x, k, n_levels);
        grad = gradient_objective_state(
                   psi_target, basis, a, t, n_penalize, penalty_weight)
                   .flat();
        return objective_state(psi_target, basis, a, t, n_penalize,
                               penalty_weight);
    }
};

// alphas ~ N(0, sqrt(n_levels)), thetas ~ U(0, pi). 
Eigen::VectorXd random_guess(int k, int n_levels, std::mt19937_64& rng) {
    std::normal_distribution<double> normal(0.0, std::sqrt((double)n_levels));
    std::uniform_real_distribution<double> uniform(0.0, M_PI);
    Eigen::VectorXd g(k + 1 + k * n_levels);
    for (int i = 0; i < k + 1; ++i) g(i) = normal(rng);
    for (int i = k + 1; i < g.size(); ++i) g(i) = uniform(rng);
    return g;
}


template <typename Objective, typename ScoreFn>
PulseResult run_multistart(Objective&& objective, ScoreFn&& score_at, int k,
                           int n_levels, const SnapOptions& opt) {
    LBFGSpp::LBFGSParam<double> lb;
    lb.epsilon        = opt.lbfgs_tol;
    lb.max_iterations = opt.lbfgs_max_iter;
    lb.max_linesearch = opt.lbfgs_max_linesearch;
    LBFGSpp::LBFGSSolver<double> solver(lb);

    std::mt19937_64 rng(opt.seed);

    double err = std::numeric_limits<double>::infinity();
    double best_boundary = 0.0;
    Eigen::VectorXd best;
    int runs_used = 0;
    int total_iterations = 0;
    int total_objective_evaluations = 0;

    Eigen::VectorXd guess =
        opt.use_initial_guess ? opt.initial_guess : random_guess(k, n_levels, rng);

    for (int j = 0; j < opt.max_runs; ++j) {
        ++runs_used;
        // From run 1 on, warm-start by averaging the running guess with fresh noise.
        if (j > 0) guess = 0.5 * (guess + random_guess(k, n_levels, rng));

        Eigen::VectorXd x = guess;
        double fx = 0.0;
        auto counted_objective = [&](const Eigen::VectorXd& values,
                                     Eigen::VectorXd& gradient) {
            ++total_objective_evaluations;
            return objective(values, gradient);
        };
        // LBFGSpp throws on line-search failure; scipy is more forgiving, so
        // treat it as a bad restart and still score the last x below.
        try {
            total_iterations += solver.minimize(counted_objective, x, fx);
        } catch (const std::exception&) {
        }

        const auto [cost, boundary] = score_at(x);
        guess = x;

        if (cost < err) {
            err = cost;
            best = x;
            best_boundary = boundary;
        }
        if (cost <= opt.err_th) {
            PulseResult candidate{x, cost, runs_used, true,
                                  total_iterations,
                                  total_objective_evaluations,
                                  boundary};
            if (!opt.candidate_acceptor ||
                opt.candidate_acceptor(candidate)) {
                return candidate;
            }
        }
    }

    if (best.size() == 0) throw std::runtime_error("optimizer produced no result.");
    return {best, err, runs_used, false, total_iterations,
            total_objective_evaluations, best_boundary};
}

}

PulseResult pulse_parameter_finder_unitary(const Matrix& U_target, int k,
                                           int n_dim, const SnapOptions& opt) {
    if (U_target.rows() != n_dim || U_target.cols() != n_dim) {
        throw std::invalid_argument("U_target size incompatible.");
    }
    const int n_levels = opt.n_levels > 0 ? opt.n_levels : n_dim;
    if (n_levels > n_dim) throw std::invalid_argument("n_levels > n_dim.");
    if (opt.d_fid < -1 || opt.d_fid == 0 || opt.d_fid > n_dim) {
        throw std::invalid_argument("d_fid must be between one and n_dim.");
    }
    validate_boundary_options(opt.n_penalize, opt.penalty_weight, n_dim);

    const DisplacementBasis basis(n_dim);
    UnitaryObjective obj{U_target, basis, k, n_levels, opt.d_fid,
                         opt.n_penalize, opt.penalty_weight};
    auto score_at = [&](const Eigen::VectorXd& x) {
        const Eigen::VectorXd a = params_to_alphas(x, k, n_levels);
        const Eigen::MatrixXd t = params_to_thetas(x, k, n_levels);
        std::optional<int> df =
            opt.d_fid > 0 ? std::optional<int>(opt.d_fid) : std::nullopt;
        return std::pair{
            cost_unitary(U_target, basis, a, t, df),
            boundary_metrics_unitary(basis, a, t, opt.n_penalize, df)
                .leakage};
    };
    return run_multistart(obj, score_at, k, n_levels, opt);
}

PulseResult pulse_parameter_finder_state(const Vector& psi_target, int k,
                                         int n_dim, const SnapOptions& opt) {
    if (psi_target.size() != n_dim) {
        throw std::invalid_argument("Target state size incompatible.");
    }
    if (std::abs(psi_target.squaredNorm() - 1.0) > 1e-9) {
        throw std::invalid_argument("Target state is not normalised.");
    }
    const int n_levels = opt.n_levels > 0 ? opt.n_levels : n_dim;
    if (n_levels > n_dim) throw std::invalid_argument("n_levels > n_dim.");
    validate_boundary_options(opt.n_penalize, opt.penalty_weight, n_dim);

    const DisplacementBasis basis(n_dim);
    StateObjective obj{psi_target, basis, k, n_levels, opt.n_penalize,
                       opt.penalty_weight};
    auto score_at = [&](const Eigen::VectorXd& x) {
        const Eigen::VectorXd a = params_to_alphas(x, k, n_levels);
        const Eigen::MatrixXd t = params_to_thetas(x, k, n_levels);
        return std::pair{
            cost_state(psi_target, basis, a, t),
            boundary_metrics_state(basis, a, t, opt.n_penalize).leakage};
    };
    return run_multistart(obj, score_at, k, n_levels, opt);
}

}
