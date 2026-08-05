#include "ecd_parameter_finder.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include <unsupported/Eigen/KroneckerProduct>
#include <unsupported/Eigen/MatrixFunctions>

#include <LBFGS.h>

#ifdef ECD_WITH_TORCH
#include <torch/torch.h>
#endif

namespace ecd {

namespace {
constexpr double LOG_EPS = 1e-12;

void validate_unitary_target(const Matrix& target, int dimension) {
    if (target.rows() != dimension || target.cols() != dimension) {
        throw std::invalid_argument("unitary target has incompatible dimensions");
    }
    const Matrix identity = Matrix::Identity(dimension, dimension);
    if (!(target.adjoint() * target).isApprox(identity, 1e-9)) {
        throw std::invalid_argument("unitary target is not unitary");
    }
}
}

Matrix displacement(Complex alpha, const Matrix& a, const Matrix& adag) {
    const Matrix generator = alpha * adag - std::conj(alpha) * a;
    return generator.exp();
}

Matrix displacement(Complex alpha, int N) {
    const Matrix a = core::annihilation(N).cast<Complex>();
    return displacement(alpha, a, a.adjoint());
}

std::pair<Matrix, Matrix> ecd(Complex beta, const Matrix& a, const Matrix& adag) {
    const Matrix D_pos = displacement(beta / 2.0, a, adag);
    const Matrix D_neg = D_pos.adjoint();
    return {D_neg, D_pos};
}

Matrix rotation_matrix(double theta, double phi) {
    const double c = std::cos(theta / 2.0);
    const double s = std::sin(theta / 2.0);
    Matrix R(2, 2);
    R(0, 0) = c;
    R(0, 1) = -Complex(0, 1) * std::exp(-Complex(0, 1) * phi) * s;
    R(1, 0) = -Complex(0, 1) * std::exp(Complex(0, 1) * phi) * s;
    R(1, 1) = c;
    return R;
}

Matrix embed_cavity(const Matrix& op, int j, int num_modes, int N) {
    const Matrix I = Matrix::Identity(N, N);
    Matrix result = (j == 0) ? op : I;
    for (int i = 1; i < num_modes; ++i) {
        result = Eigen::kroneckerProduct(result, (i == j) ? op : I).eval();
    }
    return result;
}

Eigen::MatrixXd mode_fock_probs(const Vector& psi, int num_modes, int N) {
    const int dim_cav = static_cast<int>(std::llround(std::pow(N, num_modes)));

    Eigen::VectorXd joint(dim_cav);
    for (int idx = 0; idx < dim_cav; ++idx) {
        joint(idx) = std::norm(psi(idx)) + std::norm(psi(dim_cav + idx));
    }

    Eigen::MatrixXd probs = Eigen::MatrixXd::Zero(num_modes, N);
    for (int j = 0; j < num_modes; ++j) {
        const int stride = static_cast<int>(std::llround(std::pow(N, num_modes - 1 - j)));
        for (int idx = 0; idx < dim_cav; ++idx) {
            probs(j, (idx / stride) % N) += joint(idx);
        }
    }
    return probs;
}

namespace {

struct EvolutionResult {
    Matrix ground;
    double penalty;
    double boundary;
};

EvolutionResult evolve(const Matrix& betas,
                       const Eigen::MatrixXd& rotations,
                       const Matrix& initial_ground, int N,
                       int n_penalize) {
    const int k = static_cast<int>(betas.rows());
    const int num_modes = static_cast<int>(betas.cols());
    if (rotations.rows() != k * num_modes + 1 || rotations.cols() != 2) {
        throw std::invalid_argument("rotations must be (k*num_modes+1) x 2.");
    }
    const int dim_cav = static_cast<int>(std::llround(std::pow(N, num_modes)));
    if (initial_ground.rows() != dim_cav || initial_ground.cols() == 0) {
        throw std::invalid_argument("initial ground states have incompatible dimensions");
    }
    if (n_penalize < 0 || n_penalize > N) {
        throw std::invalid_argument("n_penalize must be between zero and N");
    }

    const Matrix a = core::annihilation(N).cast<Complex>();
    const Matrix adag = a.adjoint();

    Eigen::VectorXd weights(n_penalize);
    for (int i = 0; i < n_penalize; ++i) {
        weights(i) = std::exp((N - n_penalize + i) + 1.0 - N);
    }

    Matrix excited = Matrix::Zero(dim_cav, initial_ground.cols());
    Matrix ground = initial_ground;

    auto apply_rotation = [&](int rot_row) {
        const Matrix R = rotation_matrix(rotations(rot_row, 0), rotations(rot_row, 1));
        const Matrix next_excited = R(0, 0) * excited + R(0, 1) * ground;
        const Matrix next_ground = R(1, 0) * excited + R(1, 1) * ground;
        excited = next_excited;
        ground = next_ground;
    };

    apply_rotation(0);

    double penalty = 0.0, boundary = 0.0;
    int rot_idx = 1;
    for (int layer = 0; layer < k; ++layer) {
        for (int j = 0; j < num_modes; ++j) {
            const auto [D_neg, D_pos] = ecd(betas(layer, j), a, adag);
            const Matrix D_neg_full = embed_cavity(D_neg, j, num_modes, N);
            const Matrix D_pos_full = embed_cavity(D_pos, j, num_modes, N);

            const Matrix next_excited = D_pos_full * ground;
            const Matrix next_ground = D_neg_full * excited;
            excited = next_excited;
            ground = next_ground;

            for (Eigen::Index column = 0; column < ground.cols(); ++column) {
                Vector joint(2 * dim_cav);
                joint.head(dim_cav) = excited.col(column);
                joint.tail(dim_cav) = ground.col(column);
                const Eigen::MatrixXd probs =
                    mode_fock_probs(joint, num_modes, N);
                boundary = std::max(boundary, probs.col(N - 1).maxCoeff());
                if (n_penalize > 0) {
                    penalty +=
                        (probs.rightCols(n_penalize).array().rowwise() *
                         weights.transpose().array())
                            .sum();
                }
            }

            apply_rotation(rot_idx);
            ++rot_idx;
        }
    }
    penalty /= static_cast<double>(ground.cols());
    return {ground, penalty, boundary};
}

}

CircuitResult run_circuit(const Matrix& betas, const Eigen::MatrixXd& rotations,
                          int N, int n_penalize) {
    const int num_modes = static_cast<int>(betas.cols());
    const int dim_cav = static_cast<int>(std::llround(std::pow(N, num_modes)));
    Matrix vacuum = Matrix::Zero(dim_cav, 1);
    vacuum(0, 0) = 1.0;
    const EvolutionResult result =
        evolve(betas, rotations, vacuum, N, n_penalize);
    return {result.ground.col(0), result.penalty, result.boundary};
}

Matrix logical_embedding(int d, int num_modes, int N) {
    if (d <= 0 || num_modes <= 0 || N < d) {
        throw std::invalid_argument("logical dimensions must satisfy 0 < d <= N");
    }
    const int logical_dim = static_cast<int>(std::llround(std::pow(d, num_modes)));
    const int fock_dim = static_cast<int>(std::llround(std::pow(N, num_modes)));
    Matrix embedding = Matrix::Zero(fock_dim, logical_dim);
    for (int logical_index = 0; logical_index < logical_dim; ++logical_index) {
        int fock_index = 0;
        for (int mode = 0; mode < num_modes; ++mode) {
            const int logical_stride = static_cast<int>(
                std::llround(std::pow(d, num_modes - mode - 1)));
            const int fock_stride = static_cast<int>(
                std::llround(std::pow(N, num_modes - mode - 1)));
            const int level = (logical_index / logical_stride) % d;
            fock_index += level * fock_stride;
        }
        embedding(fock_index, logical_index) = 1.0;
    }
    return embedding;
}

UnitaryCircuitResult run_unitary_circuit(const Matrix& betas,
                                         const Eigen::MatrixXd& rotations,
                                         int d, int N, int n_penalize) {
    const Matrix embedding =
        logical_embedding(d, static_cast<int>(betas.cols()), N);
    const EvolutionResult result =
        evolve(betas, rotations, embedding, N, n_penalize);
    return {result.ground, result.penalty, result.boundary};
}

double state_infidelity(const Vector& psi_target, const Vector& psi_g) {
    if (psi_target.size() != psi_g.size()) {
        throw std::invalid_argument("target and ground-block sizes differ");
    }
    if (std::abs(psi_target.squaredNorm() - 1.0) > 1e-9) {
        throw std::invalid_argument("target state is not normalized");
    }
    return 1.0 - std::abs(psi_target.dot(psi_g));
}

double unitary_infidelity(const Matrix& target, const Matrix& ground_block) {
    if (target.rows() != ground_block.rows() ||
        target.cols() != ground_block.cols() || target.cols() == 0) {
        throw std::invalid_argument("target and ground-block dimensions differ");
    }
    const Matrix identity = Matrix::Identity(target.cols(), target.cols());
    if (!(target.adjoint() * target).isApprox(identity, 1e-9)) {
        throw std::invalid_argument("target columns are not orthonormal");
    }
    const Complex overlap =
        target.conjugate().cwiseProduct(ground_block).sum() /
        static_cast<double>(target.cols());
    return 1.0 - std::abs(overlap);
}

double state_objective(const Matrix& betas, const Eigen::MatrixXd& rotations,
                       const Vector& psi_target, int N, int n_penalize,
                       double penalty_weight) {
    const CircuitResult rc = run_circuit(betas, rotations, N, n_penalize);
    const double overlap_loss = state_infidelity(psi_target, rc.psi_g);
    return std::log(overlap_loss + penalty_weight * rc.penalty + LOG_EPS);
}

double unitary_objective(const Matrix& betas,
                         const Eigen::MatrixXd& rotations,
                         const Matrix& unitary_target, int d, int N,
                         int n_penalize, double penalty_weight) {
    const int num_modes = static_cast<int>(betas.cols());
    const Matrix embedding = logical_embedding(d, num_modes, N);
    validate_unitary_target(unitary_target,
                            static_cast<int>(embedding.cols()));
    const Matrix target = embedding * unitary_target;
    const EvolutionResult rc =
        evolve(betas, rotations, embedding, N, n_penalize);
    const double overlap_loss = unitary_infidelity(target, rc.ground);
    return std::log(overlap_loss + penalty_weight * rc.penalty + LOG_EPS);
}

void unpack_params(const Eigen::VectorXd& params, int k, int num_modes,
                   Matrix& betas, Eigen::MatrixXd& rotations) {
    const int n_beta = 2 * k * num_modes;
    betas.resize(k, num_modes);
    for (int layer = 0; layer < k; ++layer)
        for (int j = 0; j < num_modes; ++j) {
            const int base = (layer * num_modes + j) * 2;
            betas(layer, j) = Complex(params(base), params(base + 1));
        }
    const int n_rot = k * num_modes + 1;
    rotations.resize(n_rot, 2);
    for (int r = 0; r < n_rot; ++r) {
        rotations(r, 0) = params(n_beta + r * 2);
        rotations(r, 1) = params(n_beta + r * 2 + 1);
    }
}

Eigen::VectorXd pack_params(const Matrix& betas,
                            const Eigen::MatrixXd& rotations) {
    const int k = static_cast<int>(betas.rows());
    const int num_modes = static_cast<int>(betas.cols());
    const int n_beta = 2 * k * num_modes;
    const int n_rot = static_cast<int>(rotations.rows());
    Eigen::VectorXd params(n_beta + 2 * n_rot);
    for (int layer = 0; layer < k; ++layer)
        for (int j = 0; j < num_modes; ++j) {
            const int base = (layer * num_modes + j) * 2;
            params(base) = betas(layer, j).real();
            params(base + 1) = betas(layer, j).imag();
        }
    for (int r = 0; r < n_rot; ++r) {
        params(n_beta + r * 2) = rotations(r, 0);
        params(n_beta + r * 2 + 1) = rotations(r, 1);
    }
    return params;
}

#ifdef ECD_WITH_TORCH
namespace {

const auto kR = torch::kFloat64;
const auto kC = torch::kComplexDouble;

torch::Tensor real_vec(const std::vector<double>& v) {
    return torch::from_blob(const_cast<double*>(v.data()),
                            {static_cast<long>(v.size())}, kR)
        .clone();
}

torch::Tensor real_vec(const Eigen::VectorXd& v) {
    return torch::from_blob(const_cast<double*>(v.data()),
                            {static_cast<long>(v.size())}, kR)
        .clone();
}

torch::Tensor real_mat(const Eigen::MatrixXd& M) {
    const int r = static_cast<int>(M.rows()), c = static_cast<int>(M.cols());
    std::vector<double> buf(static_cast<size_t>(r) * c);
    for (int i = 0; i < r; ++i)
        for (int j = 0; j < c; ++j) buf[static_cast<size_t>(i) * c + j] = M(i, j);
    return torch::from_blob(buf.data(), {r, c}, kR).clone();
}

torch::Tensor complex_mat(const Matrix& matrix) {
    const int rows = static_cast<int>(matrix.rows());
    const int cols = static_cast<int>(matrix.cols());
    std::vector<double> real(static_cast<size_t>(rows) * cols);
    std::vector<double> imag(static_cast<size_t>(rows) * cols);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const size_t index = static_cast<size_t>(row) * cols + col;
            real[index] = matrix(row, col).real();
            imag[index] = matrix(row, col).imag();
        }
    }
    return torch::complex(
        torch::from_blob(real.data(), {rows, cols}, kR).clone(),
        torch::from_blob(imag.data(), {rows, cols}, kR).clone());
}

std::vector<double> penalty_weights(int num_modes, int N, int n_penalize) {
    const int dim = static_cast<int>(std::llround(std::pow(N, num_modes)));
    std::vector<double> W(dim, 0.0);
    if (n_penalize <= 0) return W;
    for (int idx = 0; idx < dim; ++idx) {
        double w = 0.0;
        for (int j = 0; j < num_modes; ++j) {
            const int stride =
                static_cast<int>(std::llround(std::pow(N, num_modes - 1 - j)));
            const int level = (idx / stride) % N;
            if (level >= N - n_penalize) w += std::exp((level + 1.0) - N);
        }
        W[idx] = w;
    }
    return W;
}

Eigen::VectorXd torch_gradient(const Matrix& betas,
                               const Eigen::MatrixXd& rotations,
                               const Matrix& initial_ground,
                               const Matrix& target, int N,
                               int n_penalize, double penalty_weight) {
    torch::AutoGradMode grad_on(true);

    const int k = static_cast<int>(betas.rows());
    const int m = static_cast<int>(betas.cols());
    const int L = k * m;
    if (rotations.rows() != L + 1 || rotations.cols() != 2) {
        throw std::invalid_argument("rotations must be (k*num_modes+1) x 2.");
    }
    const int dim = static_cast<int>(std::llround(std::pow(N, m)));
    if (initial_ground.rows() != dim || target.rows() != dim ||
        initial_ground.cols() != target.cols() || target.cols() == 0) {
        throw std::invalid_argument("torch objective matrices have incompatible dimensions");
    }
    const int n_beta = 2 * k * m;
    const int P = n_beta + 2 * (L + 1);

    const Eigen::VectorXd packed = pack_params(betas, rotations);
    torch::Tensor params = real_vec(packed).set_requires_grad(true);

    const Eigen::MatrixXd a_eig = core::annihilation(N);
    const torch::Tensor a = torch::complex(real_mat(a_eig),
                                           torch::zeros({N, N}, kR));
    const torch::Tensor adag = torch::conj(a).transpose(0, 1).contiguous();
    const torch::Tensor eye = torch::complex(torch::eye(N, kR),
                                             torch::zeros({N, N}, kR));
    const torch::Tensor imag_i =
        torch::complex(torch::zeros({}, kR), torch::ones({}, kR));
    const torch::Tensor Wdiag =
        real_vec(penalty_weights(m, N, n_penalize)).to(kC);
    const torch::Tensor tgt = complex_mat(target);

    auto embed = [&](const torch::Tensor& op_in, int j) {
        const torch::Tensor op = op_in.contiguous();
        torch::Tensor out = (j == 0) ? op : eye;
        for (int i = 1; i < m; ++i) out = torch::kron(out, (i == j) ? op : eye);
        return out;
    };

    auto rotation = [&](int r) {
        const torch::Tensor theta = params[n_beta + r * 2];
        const torch::Tensor phi = params[n_beta + r * 2 + 1];
        const torch::Tensor c = torch::cos(theta / 2).to(kC);
        const torch::Tensor s = torch::sin(theta / 2).to(kC);
        const torch::Tensor eneg =
            torch::complex(torch::cos(phi), -torch::sin(phi));
        const torch::Tensor epos =
            torch::complex(torch::cos(phi), torch::sin(phi));
        struct R { torch::Tensor r00, r01, r10, r11; };
        return R{c, -imag_i * eneg * s, -imag_i * epos * s, c};
    };
    auto apply_rot = [&](int r, torch::Tensor& e, torch::Tensor& g) {
        const auto R = rotation(r);
        const torch::Tensor ne = R.r00 * e + R.r01 * g;
        const torch::Tensor ng = R.r10 * e + R.r11 * g;
        e = ne;
        g = ng;
    };

    torch::Tensor g = complex_mat(initial_ground);
    torch::Tensor e = torch::zeros_like(g);
    apply_rot(0, e, g);

    torch::Tensor penalty = torch::zeros({}, kR);
    int s = 0;
    for (int layer = 0; layer < k; ++layer) {
        for (int j = 0; j < m; ++j) {
            ++s;
            const torch::Tensor beta =
                torch::complex(params[(layer * m + j) * 2],
                               params[(layer * m + j) * 2 + 1]);
            const torch::Tensor gen =
                (beta / 2) * adag - torch::conj(beta / 2) * a;
            const torch::Tensor D = torch::matrix_exp(gen);
            const torch::Tensor Dp = embed(D, j);
            const torch::Tensor Dn = embed(torch::conj(D).transpose(0, 1), j);

            const torch::Tensor new_e = torch::matmul(Dp, g);
            const torch::Tensor new_g = torch::matmul(Dn, e);
            e = new_e;
            g = new_g;

            if (n_penalize > 0) {
                const torch::Tensor pop =
                    torch::abs(e).pow(2) + torch::abs(g).pow(2);
                penalty = penalty +
                    torch::sum(torch::real(Wdiag).unsqueeze(1) * pop) /
                    target.cols();
            }
            apply_rot(s, e, g);
        }
    }

    const torch::Tensor overlap =
        torch::sum(torch::conj(tgt) * g) / target.cols();
    const torch::Tensor overlap_loss = 1.0 - torch::abs(overlap);
    const torch::Tensor loss =
        torch::log(overlap_loss + penalty_weight * penalty + LOG_EPS);

    loss.backward();

    const torch::Tensor grad = params.grad().contiguous().to(kR);
    Eigen::VectorXd out(P);
    auto acc = grad.accessor<double, 1>();
    for (int i = 0; i < P; ++i) out(i) = acc[i];
    return out;
}

}

Eigen::VectorXd torch_objective_gradient(const Matrix& betas,
                                         const Eigen::MatrixXd& rotations,
                                         const Vector& psi_target, int N,
                                         int n_penalize, double penalty_weight) {
    if (std::abs(psi_target.squaredNorm() - 1.0) > 1e-9) {
        throw std::invalid_argument("state target is not normalized");
    }
    Matrix vacuum = Matrix::Zero(psi_target.size(), 1);
    vacuum(0, 0) = 1.0;
    const Matrix target = psi_target;
    return torch_gradient(betas, rotations, vacuum, target, N, n_penalize,
                          penalty_weight);
}

Eigen::VectorXd torch_unitary_objective_gradient(
    const Matrix& betas, const Eigen::MatrixXd& rotations,
    const Matrix& unitary_target, int d, int N, int n_penalize,
    double penalty_weight) {
    const Matrix embedding =
        logical_embedding(d, static_cast<int>(betas.cols()), N);
    validate_unitary_target(unitary_target,
                            static_cast<int>(embedding.cols()));
    return torch_gradient(betas, rotations, embedding,
                          embedding * unitary_target, N, n_penalize,
                          penalty_weight);
}
#endif

namespace {
template <typename Objective>
Eigen::VectorXd fd_gradient(const Eigen::VectorXd& params,
                            Objective&& objective, double h) {
    Eigen::VectorXd g(params.size());
    if (params.size() == 0) return g;

    const unsigned int available =
        std::max(1U, std::thread::hardware_concurrency());
    const int worker_count = std::min<int>(
        params.size(), static_cast<int>(available));
    std::atomic<int> next_index{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    for (int worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            Eigen::VectorXd shifted = params;
            while (true) {
                const int index = next_index.fetch_add(1);
                if (index >= params.size()) break;
                shifted(index) = params(index) + h;
                const double upper = objective(shifted);
                shifted(index) = params(index) - h;
                const double lower = objective(shifted);
                shifted(index) = params(index);
                g(index) = (upper - lower) / (2.0 * h);
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    return g;
}

struct OptimizationResult {
    Eigen::VectorXd params;
    double error;
    int restarts_used;
    int iterations;
    int objective_evaluations;
    bool accepted;
};

template <typename Objective, typename Gradient, typename Score,
          typename Acceptor>
OptimizationResult optimize_parameters(
    int k, int num_modes, uint64_t seed, const ECDOptions& options,
    Objective&& objective, Gradient&& gradient, Score&& score,
    Acceptor&& acceptor) {
    const int n_beta = 2 * k * num_modes;
    const int parameter_count = n_beta + 2 * (k * num_modes + 1);
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> normal(0.0, 1.0);
    std::uniform_real_distribution<double> uniform(0.0, 2.0 * std::acos(-1.0));

    LBFGSpp::LBFGSParam<double> lbfgs_options;
    lbfgs_options.epsilon = options.lbfgs_tol;
    lbfgs_options.max_iterations = options.lbfgs_max_iter;
    lbfgs_options.max_linesearch = options.lbfgs_max_linesearch;
    LBFGSpp::LBFGSSolver<double> solver(lbfgs_options);

    double best_error = std::numeric_limits<double>::infinity();
    Eigen::VectorXd best_params;
    Eigen::VectorXd previous;
    int total_iterations = 0;
    int total_objective_evaluations = 0;
    int restarts_used = 0;

    if (options.use_initial_guess &&
        options.initial_guess.size() != parameter_count) {
        throw std::invalid_argument(
            "ECD initial guess has incompatible dimensions");
    }

    for (int restart = 0; restart < options.n_restarts; ++restart) {
        ++restarts_used;
        Eigen::VectorXd params;
        if (restart == 0 && options.use_initial_guess) {
            params = options.initial_guess;
        } else {
            Eigen::VectorXd cold(parameter_count);
            for (int i = 0; i < n_beta; ++i) cold(i) = normal(rng);
            for (int i = n_beta; i < parameter_count; ++i) {
                cold(i) = uniform(rng);
            }
            params = previous.size() == parameter_count
                         ? 0.5 * (previous + cold)
                         : cold;
        }
        int restart_evaluations = 0;
        auto lbfgs_objective = [&](const Eigen::VectorXd& values,
                                   Eigen::VectorXd& grad) {
            ++restart_evaluations;
            grad = gradient(values);
            return objective(values);
        };

        double value = 0.0;
        try {
            total_iterations += solver.minimize(lbfgs_objective, params, value);
        } catch (const std::exception&) {
            // A failed line search invalidates this restart, not the best result
            // already found. Score the last iterate and continue from it.
        }
        total_objective_evaluations += restart_evaluations;
        previous = params;

        const double error = score(params);
        if (error < best_error) {
            best_error = error;
            best_params = params;
        }
        if (error <= options.err_th) {
            const OptimizationResult candidate{
                params, error, restarts_used, total_iterations,
                total_objective_evaluations, true};
            if (acceptor(candidate)) return candidate;
        }
    }

    if (best_params.size() == 0) {
        throw std::runtime_error("ECD optimizer produced no result");
    }
    return OptimizationResult{best_params, best_error, restarts_used,
                              total_iterations, total_objective_evaluations,
                              false};
}
}

ECDParameterFinder::ECDParameterFinder(int d, int num_modes, ECDOptions opt)
    : d_(d), num_modes_(num_modes), opt_(opt) {}

Vector ECDParameterFinder::pad_state(const Vector& target, int N) const {
    const Matrix embedding = logical_embedding(d_, num_modes_, N);
    if (target.size() != embedding.cols()) {
        throw std::invalid_argument("state target has incompatible dimensions");
    }
    if (std::abs(target.squaredNorm() - 1.0) > 1e-9) {
        throw std::invalid_argument("state target is not normalized");
    }
    return embedding * target;
}

double ECDParameterFinder::state_infidelity(const Eigen::VectorXd& params,
                                            const Vector& psi_target, int k,
                                            int N) const {
    Matrix betas;
    Eigen::MatrixXd rotations;
    unpack_params(params, k, num_modes_, betas, rotations);
    const CircuitResult rc = run_circuit(betas, rotations, N, opt_.n_penalize);
    return ecd::state_infidelity(psi_target, rc.psi_g);
}

double ECDParameterFinder::unitary_infidelity(
    const Eigen::VectorXd& params, const Matrix& unitary_target, int k,
    int N) const {
    Matrix betas;
    Eigen::MatrixXd rotations;
    unpack_params(params, k, num_modes_, betas, rotations);
    const Matrix embedding = logical_embedding(d_, num_modes_, N);
    validate_unitary_target(unitary_target,
                            static_cast<int>(embedding.cols()));
    const Matrix target = embedding * unitary_target;
    const EvolutionResult rc =
        evolve(betas, rotations, embedding, N, opt_.n_penalize);
    return ecd::unitary_infidelity(target, rc.ground);
}

Eigen::VectorXd ECDParameterFinder::state_gradient(
    const Eigen::VectorXd& params, const Vector& psi_target, int k,
    int N) const {
    switch (opt_.grad_method) {
        case GradMethod::FiniteDifference: {
            auto objective_at = [&](const Eigen::VectorXd& values) {
                Matrix betas;
                Eigen::MatrixXd rotations;
                unpack_params(values, k, num_modes_, betas, rotations);
                return state_objective(betas, rotations, psi_target, N,
                                       opt_.n_penalize, opt_.penalty_weight);
            };
            return fd_gradient(params, objective_at, opt_.fd_step);
        }
        case GradMethod::Autodiff: {
#ifdef ECD_WITH_TORCH
            Matrix betas;
            Eigen::MatrixXd rotations;
            unpack_params(params, k, num_modes_, betas, rotations);
            return torch_objective_gradient(betas, rotations, psi_target, N,
                                            opt_.n_penalize, opt_.penalty_weight);
#else
            throw std::runtime_error(
                "GradMethod::Autodiff needs a build with -DECD_WITH_TORCH=ON.");
#endif
        }
    }
    throw std::runtime_error("unknown GradMethod.");
}

Eigen::VectorXd ECDParameterFinder::unitary_gradient(
    const Eigen::VectorXd& params, const Matrix& unitary_target, int k,
    int N) const {
    switch (opt_.grad_method) {
        case GradMethod::FiniteDifference: {
            auto objective_at = [&](const Eigen::VectorXd& values) {
                Matrix betas;
                Eigen::MatrixXd rotations;
                unpack_params(values, k, num_modes_, betas, rotations);
                return unitary_objective(betas, rotations, unitary_target, d_, N,
                                         opt_.n_penalize,
                                         opt_.penalty_weight);
            };
            return fd_gradient(params, objective_at, opt_.fd_step);
        }
        case GradMethod::Autodiff: {
#ifdef ECD_WITH_TORCH
            Matrix betas;
            Eigen::MatrixXd rotations;
            unpack_params(params, k, num_modes_, betas, rotations);
            return torch_unitary_objective_gradient(
                betas, rotations, unitary_target, d_, N, opt_.n_penalize,
                opt_.penalty_weight);
#else
            throw std::runtime_error(
                "GradMethod::Autodiff needs a build with -DECD_WITH_TORCH=ON.");
#endif
        }
    }
    throw std::runtime_error("unknown GradMethod.");
}

std::optional<CompiledCircuit> ECDParameterFinder::find_state_parameters(
    const Vector& target, int k, int N, uint64_t seed) const {
    CompiledCircuit result = attempt_state_parameters(target, k, N, seed);
    if (!result.accepted) return std::nullopt;
    return result;
}

CompiledCircuit ECDParameterFinder::attempt_state_parameters(
    const Vector& target, int k, int N, uint64_t seed) const {
    const Vector psi_target = pad_state(target, N);
    auto objective = [&](const Eigen::VectorXd& params) {
        Matrix betas;
        Eigen::MatrixXd rotations;
        unpack_params(params, k, num_modes_, betas, rotations);
        return state_objective(betas, rotations, psi_target, N,
                               opt_.n_penalize, opt_.penalty_weight);
    };
    auto gradient = [&](const Eigen::VectorXd& params) {
        return state_gradient(params, psi_target, k, N);
    };
    auto score = [&](const Eigen::VectorXd& params) {
        return state_infidelity(params, psi_target, k, N);
    };
    auto acceptor = [&](const OptimizationResult& candidate) {
        if (!opt_.candidate_acceptor) return true;
        Matrix betas;
        Eigen::MatrixXd rotations;
        unpack_params(candidate.params, k, num_modes_, betas, rotations);
        const CircuitResult rc =
            run_circuit(betas, rotations, N, opt_.n_penalize);
        const CompiledCircuit circuit{
            betas,
            rotations,
            candidate.error,
            rc.boundary,
            candidate.restarts_used,
            candidate.iterations,
            candidate.objective_evaluations,
            false};
        return opt_.candidate_acceptor(circuit);
    };
    const OptimizationResult result = optimize_parameters(
        k, num_modes_, seed, opt_, objective, gradient, score, acceptor);

    Matrix betas;
    Eigen::MatrixXd rotations;
    unpack_params(result.params, k, num_modes_, betas, rotations);
    const CircuitResult rc = run_circuit(betas, rotations, N, opt_.n_penalize);
    return CompiledCircuit{betas,
                           rotations,
                           result.error,
                           rc.boundary,
                           result.restarts_used,
                           result.iterations,
                           result.objective_evaluations,
                           result.accepted};
}

std::optional<CompiledCircuit> ECDParameterFinder::find_unitary_parameters(
    const Matrix& target, int k, int N, uint64_t seed) const {
    CompiledCircuit result = attempt_unitary_parameters(target, k, N, seed);
    if (!result.accepted) return std::nullopt;
    return result;
}

CompiledCircuit ECDParameterFinder::attempt_unitary_parameters(
    const Matrix& target, int k, int N, uint64_t seed) const {
    const int logical_dim = static_cast<int>(
        std::llround(std::pow(d_, num_modes_)));
    validate_unitary_target(target, logical_dim);
    auto objective = [&](const Eigen::VectorXd& params) {
        Matrix betas;
        Eigen::MatrixXd rotations;
        unpack_params(params, k, num_modes_, betas, rotations);
        return unitary_objective(betas, rotations, target, d_, N,
                                 opt_.n_penalize, opt_.penalty_weight);
    };
    auto gradient = [&](const Eigen::VectorXd& params) {
        return unitary_gradient(params, target, k, N);
    };
    auto score = [&](const Eigen::VectorXd& params) {
        return unitary_infidelity(params, target, k, N);
    };
    auto acceptor = [&](const OptimizationResult& candidate) {
        if (!opt_.candidate_acceptor) return true;
        Matrix betas;
        Eigen::MatrixXd rotations;
        unpack_params(candidate.params, k, num_modes_, betas, rotations);
        const UnitaryCircuitResult rc = run_unitary_circuit(
            betas, rotations, d_, N, opt_.n_penalize);
        const CompiledCircuit circuit{
            betas,
            rotations,
            candidate.error,
            rc.boundary,
            candidate.restarts_used,
            candidate.iterations,
            candidate.objective_evaluations,
            false};
        return opt_.candidate_acceptor(circuit);
    };
    const OptimizationResult result = optimize_parameters(
        k, num_modes_, seed, opt_, objective, gradient, score, acceptor);

    Matrix betas;
    Eigen::MatrixXd rotations;
    unpack_params(result.params, k, num_modes_, betas, rotations);
    const UnitaryCircuitResult rc =
        run_unitary_circuit(betas, rotations, d_, N, opt_.n_penalize);
    return CompiledCircuit{betas,
                           rotations,
                           result.error,
                           rc.boundary,
                           result.restarts_used,
                           result.iterations,
                           result.objective_evaluations,
                           result.accepted};
}

std::optional<CompiledCircuit>
ECDParameterFinder::find_state_parameters_adaptive_k(
    const Vector& target, int k_init, int k_max, int N, int k_step,
    uint64_t seed) const {
    for (int k = k_init; k <= k_max; k += k_step) {
        auto circuit = find_state_parameters(target, k, N, seed);
        if (circuit.has_value()) return circuit;
    }
    return std::nullopt;
}

std::optional<CompiledCircuit>
ECDParameterFinder::find_unitary_parameters_adaptive_k(
    const Matrix& target, int k_init, int k_max, int N, int k_step,
    uint64_t seed) const {
    for (int k = k_init; k <= k_max; k += k_step) {
        auto circuit = find_unitary_parameters(target, k, N, seed);
        if (circuit.has_value()) return circuit;
    }
    return std::nullopt;
}

}
