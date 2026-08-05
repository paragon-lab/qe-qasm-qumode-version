#include "decomposition.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ecd_parameter_finder.hpp"
#include "snap_parameter_finder.hpp"

namespace decomp {
namespace {

enum class Preparation {
    Unitary,
    State,
};

int checked_power(int base, int exponent) {
    int result = 1;
    for (int i = 0; i < exponent; ++i) {
        if (base != 0 && result > std::numeric_limits<int>::max() / base) {
            throw std::invalid_argument("Fock-space dimension is too large");
        }
        result *= base;
    }
    return result;
}

int checked_sum(int left, int right, const char* message) {
    if (right > std::numeric_limits<int>::max() - left) {
        throw std::invalid_argument(message);
    }
    return left + right;
}

int compare_power(int base, int exponent, int target) {
    int result = 1;
    for (int i = 0; i < exponent; ++i) {
        if (base != 0 && result > target / base) return 1;
        result *= base;
    }
    if (result < target) return -1;
    if (result > target) return 1;
    return 0;
}

int infer_levels(Eigen::Index dimension, int modes) {
    if (dimension <= 0 || dimension > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("target dimension must be positive and fit in int");
    }
    const int target = static_cast<int>(dimension);
    if (modes == 1) return target;

    int low = 1;
    int high = target;
    while (low <= high) {
        const int middle = low + (high - low) / 2;
        const int comparison = compare_power(middle, modes, target);
        if (comparison == 0) return middle;
        if (comparison < 0) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    throw std::invalid_argument(
        "target dimension is not an exact integer power for the supplied modes");
}

void validate_options(const Options& options, int modes, GateSet gate_set) {
    if (modes <= 0) throw std::invalid_argument("modes must be positive");
    if (gate_set == GateSet::Snap && modes != 1) {
        throw std::invalid_argument("SNAP supports exactly one mode");
    }
    if (options.layers && *options.layers < 0) {
        throw std::invalid_argument("layers cannot be negative");
    }
    if (options.buffers && *options.buffers < 0) {
        throw std::invalid_argument("buffers cannot be negative");
    }
    if (options.max_layers < 0) {
        throw std::invalid_argument("max_layers cannot be negative");
    }
    if (options.max_buffers < 0) {
        throw std::invalid_argument("max_buffers cannot be negative");
    }
    if (options.replay_increment < 12) {
        throw std::invalid_argument("replay_increment must be at least 12");
    }
    if (options.max_restarts <= 0) {
        throw std::invalid_argument("max_restarts must be positive");
    }
    if (!std::isfinite(options.optimization_threshold) ||
        options.optimization_threshold < 0.0) {
        throw std::invalid_argument(
            "optimization_threshold must be finite and nonnegative");
    }
    if (!std::isfinite(options.stability_threshold) ||
        options.stability_threshold < 0.0) {
        throw std::invalid_argument(
            "stability_threshold must be finite and nonnegative");
    }
    if (!std::isfinite(options.lbfgs_tolerance) ||
        options.lbfgs_tolerance <= 0.0) {
        throw std::invalid_argument(
            "lbfgs_tolerance must be finite and positive");
    }
    if (options.lbfgs_max_iterations <= 0 ||
        options.lbfgs_max_linesearch <= 0) {
        throw std::invalid_argument(
            "L-BFGS iteration and line-search limits must be positive");
    }
    if (options.ecd.n_penalize < -1) {
        throw std::invalid_argument("ECD n_penalize cannot be less than -1");
    }
    if (!std::isfinite(options.ecd.penalty_weight) ||
        options.ecd.penalty_weight < 0.0) {
        throw std::invalid_argument(
            "ECD penalty_weight must be finite and nonnegative");
    }
    if (!std::isfinite(options.ecd.finite_difference_step) ||
        options.ecd.finite_difference_step <= 0.0) {
        throw std::invalid_argument(
            "ECD finite_difference_step must be finite and positive");
    }
    if (options.ecd.warm_start_layers &&
        *options.ecd.warm_start_layers < 0) {
        throw std::invalid_argument(
            "ECD warm_start_layers cannot be negative");
    }
    if (options.ecd.warm_start_layers && !options.layers) {
        throw std::invalid_argument(
            "ECD warm_start_layers requires fixed layers");
    }
    if (options.ecd.warm_start_layers && options.layers &&
        *options.ecd.warm_start_layers >= *options.layers) {
        throw std::invalid_argument(
            "ECD warm_start_layers must be smaller than layers");
    }
    if (options.snap.n_penalize < -1) {
        throw std::invalid_argument("SNAP n_penalize cannot be less than -1");
    }
    if (!std::isfinite(options.snap.penalty_weight) ||
        options.snap.penalty_weight < 0.0) {
        throw std::invalid_argument(
            "SNAP penalty_weight must be finite and nonnegative");
    }
#ifndef ECD_WITH_TORCH
    if (gate_set == GateSet::Ecd &&
        options.ecd.gradient_method == GradientMethod::Autodiff) {
        throw std::invalid_argument(
            "ECD autodiff requires a build with -DECD_WITH_TORCH=ON");
    }
#endif
}

int ceil_div(long long numerator, long long denominator) {
    if (numerator <= 0) return 0;
    const long long result = (numerator + denominator - 1) / denominator;
    if (result > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("automatic layer count is too large");
    }
    return static_cast<int>(result);
}

int starting_layers(int logical_dimension, int levels, int modes,
                    GateSet gate_set, Preparation preparation) {
    long long remaining_degrees = 0;
    long long per_layer = 0;
    if (preparation == Preparation::Unitary) {
        remaining_degrees =
            static_cast<long long>(logical_dimension) * logical_dimension - 1;
    } else {
        remaining_degrees = 2LL * logical_dimension - 2;
    }
    if (gate_set == GateSet::Ecd) {
        remaining_degrees -= 2;
        per_layer = 4LL * modes;
    } else {
        remaining_degrees -= 1;
        per_layer = levels + 1LL;
    }
    const int floor = ceil_div(remaining_degrees, per_layer);
    if (floor > std::numeric_limits<int>::max() / 2) {
        throw std::invalid_argument("automatic layer count is too large");
    }
    return std::max(4, 2 * floor);
}

int maximum_layers(const Options& options, int start) {
    if (options.layers) return *options.layers;
    if (options.max_layers != 0) {
        if (options.max_layers < start) {
            throw std::invalid_argument(
                "max_layers is smaller than the automatic starting depth");
        }
        return options.max_layers;
    }
    if (start > std::numeric_limits<int>::max() / 3) {
        throw std::invalid_argument("automatic layer limit is too large");
    }
    return 3 * start;
}

int automatic_starting_buffers() {
    return 2;
}

int automatic_n_penalize(int configured, int buffers, int cutoff,
                         const char* gate_name) {
    const int value = configured >= 0
                          ? configured
                          : (buffers == 0 ? 0 : std::max(1, buffers / 2));
    if (value > cutoff) {
        throw std::invalid_argument(std::string(gate_name) +
                                    " n_penalize exceeds the cutoff");
    }
    return value;
}

snap::SnapOptions snap_options(const Options& options, int levels,
                               int cutoff, uint64_t seed) {
    snap::SnapOptions result;
    result.n_levels = cutoff;
    result.max_runs = options.max_restarts;
    result.err_th = options.optimization_threshold;
    result.d_fid = levels;
    result.lbfgs_tol = options.lbfgs_tolerance;
    result.lbfgs_max_iter = options.lbfgs_max_iterations;
    result.lbfgs_max_linesearch = options.lbfgs_max_linesearch;
    result.seed = seed;
    result.n_penalize = automatic_n_penalize(
        options.snap.n_penalize, cutoff - levels, cutoff, "SNAP");
    result.penalty_weight = options.snap.penalty_weight;
    return result;
}

ecd::ECDOptions ecd_options(const Options& options, int buffers, int cutoff,
                            uint64_t seed) {
    ecd::ECDOptions result;
    result.n_penalize = automatic_n_penalize(
        options.ecd.n_penalize, buffers, cutoff, "ECD");
    result.penalty_weight = options.ecd.penalty_weight;
    result.err_th = options.optimization_threshold;
    result.n_restarts = options.max_restarts;
    result.lbfgs_tol = options.lbfgs_tolerance;
    result.lbfgs_max_iter = options.lbfgs_max_iterations;
    result.lbfgs_max_linesearch = options.lbfgs_max_linesearch;
    result.seed = seed;
    result.fd_step = options.ecd.finite_difference_step;
    if (options.ecd.gradient_method == GradientMethod::FiniteDifference) {
        result.grad_method = ecd::GradMethod::FiniteDifference;
    } else if (options.ecd.gradient_method == GradientMethod::Autodiff) {
        result.grad_method = ecd::GradMethod::Autodiff;
    } else {
#ifdef ECD_WITH_TORCH
        result.grad_method = ecd::GradMethod::Autodiff;
#else
        result.grad_method = ecd::GradMethod::FiniteDifference;
#endif
    }
    return result;
}

double replay_error(const Matrix& target, const Result& compiled, int levels,
                    int modes, int cutoff);

double replay_error(const Vector& target, const Result& compiled, int levels,
                    int modes, int cutoff);

uint64_t configuration_seed(uint64_t base_seed, int layers, int buffers);

struct ReplaySelection {
    double                   error = std::numeric_limits<double>::infinity();
    std::vector<ReplayCheck> checks;
};

template <typename Target>
bool evaluate_replay_candidate(const Target& target, Result& candidate,
                               int levels, int modes,
                               const Options& options,
                               ReplaySelection& selection) {
    candidate.replay_error = 0.0;
    candidate.replay_checks.clear();
    candidate.replay_checks.reserve(
        static_cast<std::size_t>(options.replay_increment));
    for (int increment = 1; increment <= options.replay_increment;
         ++increment) {
        const int replay_cutoff = checked_sum(
            candidate.cutoff_per_mode, increment,
            "replay cutoff is too large");
        checked_power(replay_cutoff, modes);
        double error =
            replay_error(target, candidate, levels, modes, replay_cutoff);
        if (!std::isfinite(error)) {
            error = std::numeric_limits<double>::infinity();
        }
        candidate.replay_checks.push_back({replay_cutoff, error});
        candidate.replay_error = std::max(candidate.replay_error, error);
    }
    if (candidate.replay_error < selection.error) {
        selection.error = candidate.replay_error;
        selection.checks = candidate.replay_checks;
    }
    return candidate.replay_error <= options.stability_threshold;
}

void apply_replay_selection(Result& result,
                            const ReplaySelection& selection) {
    result.replay_error = selection.error;
    result.replay_checks = selection.checks;
}

ecd::CompiledCircuit extend_ecd_circuit(
    const ecd::CompiledCircuit& source, int layers) {
    const int source_layers = static_cast<int>(source.betas.rows());
    const int modes = static_cast<int>(source.betas.cols());
    if (layers < source_layers || modes <= 0) {
        throw std::invalid_argument("invalid ECD warm-start extension");
    }
    ecd::CompiledCircuit result;
    result.betas = Matrix::Zero(layers, modes);
    result.betas.topRows(source_layers) = source.betas;
    result.rotations = Eigen::MatrixXd::Zero(layers * modes + 1, 2);
    result.rotations.topRows(source.rotations.rows()) = source.rotations;
    for (Eigen::Index row = source.rotations.rows();
         row < result.rotations.rows(); ++row) {
        result.rotations(row, 0) = std::acos(-1.0);
    }
    return result;
}

Result compile_at(const Matrix& target, int levels, int modes,
                  GateSet gate_set, int layers, int buffers,
                  const Options& options, uint64_t seed) {
    const int cutoff =
        checked_sum(levels, buffers, "per-mode cutoff is too large");
    checked_power(cutoff, modes);
    if (gate_set == GateSet::Snap) {
        Matrix padded = Matrix::Identity(cutoff, cutoff);
        padded.topLeftCorner(target.rows(), target.cols()) = target;
        snap::SnapOptions finder_options =
            snap_options(options, levels, cutoff, seed);
        ReplaySelection replay_selection;
        finder_options.candidate_acceptor =
            [&](const snap::PulseResult& candidate) {
                Result replay_candidate;
                replay_candidate.gate_set = gate_set;
                replay_candidate.circuit = SnapCircuit{
                    candidate.alphas(layers, cutoff),
                    candidate.thetas(layers, cutoff)};
                replay_candidate.error = candidate.err;
                replay_candidate.layers = layers;
                replay_candidate.buffers = buffers;
                replay_candidate.cutoff_per_mode = cutoff;
                replay_candidate.boundary_leakage =
                    candidate.boundary_leakage;
                return evaluate_replay_candidate(
                    target, replay_candidate, levels, modes, options,
                    replay_selection);
            };
        const snap::PulseResult compiled =
            snap::pulse_parameter_finder_unitary(
                padded, layers, cutoff, finder_options);
        Result result;
        result.gate_set = gate_set;
        result.circuit = SnapCircuit{compiled.alphas(layers, cutoff),
                                     compiled.thetas(layers, cutoff)};
        result.error = compiled.err;
        result.converged = compiled.converged;
        result.layers = layers;
        result.buffers = buffers;
        result.cutoff_per_mode = cutoff;
        result.restarts_used = compiled.runs_used;
        result.iterations = compiled.iterations;
        result.objective_evaluations = compiled.objective_evaluations;
        result.boundary_leakage = compiled.boundary_leakage;
        apply_replay_selection(result, replay_selection);
        return result;
    }

    ecd::ECDOptions finder_options =
        ecd_options(options, buffers, cutoff, seed);
    int warm_restarts = 0;
    int warm_iterations = 0;
    int warm_evaluations = 0;
    if (options.ecd.warm_start_layers) {
        const int warm_layers = *options.ecd.warm_start_layers;
        ecd::ECDOptions warm_options = finder_options;
        warm_options.candidate_acceptor = {};
        warm_options.use_initial_guess = false;
        const ecd::ECDParameterFinder warm_finder(levels, modes,
                                                   warm_options);
        const ecd::CompiledCircuit warm =
            warm_finder.attempt_unitary_parameters(
                target, warm_layers, cutoff,
                configuration_seed(options.seed, warm_layers, buffers));
        const ecd::CompiledCircuit extended =
            extend_ecd_circuit(warm, layers);
        finder_options.use_initial_guess = true;
        finder_options.initial_guess =
            ecd::pack_params(extended.betas, extended.rotations);
        warm_restarts = warm.restarts_used;
        warm_iterations = warm.iterations;
        warm_evaluations = warm.objective_evaluations;
    }
    ReplaySelection replay_selection;
    finder_options.candidate_acceptor =
        [&](const ecd::CompiledCircuit& candidate) {
            Result replay_candidate;
            replay_candidate.gate_set = gate_set;
            replay_candidate.circuit =
                ECDCircuit{candidate.betas, candidate.rotations};
            replay_candidate.error = candidate.err;
            replay_candidate.layers = layers;
            replay_candidate.buffers = buffers;
            replay_candidate.cutoff_per_mode = cutoff;
            replay_candidate.boundary_leakage =
                candidate.boundary_leakage;
            return evaluate_replay_candidate(
                target, replay_candidate, levels, modes, options,
                replay_selection);
        };
    const ecd::ECDParameterFinder finder(levels, modes, finder_options);
    const ecd::CompiledCircuit compiled = finder.attempt_unitary_parameters(
        target, layers, cutoff, seed);
    Result result;
    result.gate_set = gate_set;
    result.circuit = ECDCircuit{compiled.betas, compiled.rotations};
    result.error = compiled.err;
    result.converged = compiled.accepted;
    result.layers = layers;
    result.buffers = buffers;
    result.cutoff_per_mode = cutoff;
    result.restarts_used = warm_restarts + compiled.restarts_used;
    result.iterations = warm_iterations + compiled.iterations;
    result.objective_evaluations =
        warm_evaluations + compiled.objective_evaluations;
    result.boundary_leakage = compiled.boundary_leakage;
    apply_replay_selection(result, replay_selection);
    return result;
}

Result compile_at(const Vector& target, int levels, int modes,
                  GateSet gate_set, int layers, int buffers,
                  const Options& options, uint64_t seed) {
    const int cutoff =
        checked_sum(levels, buffers, "per-mode cutoff is too large");
    checked_power(cutoff, modes);
    if (gate_set == GateSet::Snap) {
        Vector padded = Vector::Zero(cutoff);
        padded.head(target.size()) = target;
        snap::SnapOptions finder_options =
            snap_options(options, levels, cutoff, seed);
        ReplaySelection replay_selection;
        finder_options.candidate_acceptor =
            [&](const snap::PulseResult& candidate) {
                Result replay_candidate;
                replay_candidate.gate_set = gate_set;
                replay_candidate.circuit = SnapCircuit{
                    candidate.alphas(layers, cutoff),
                    candidate.thetas(layers, cutoff)};
                replay_candidate.error = candidate.err;
                replay_candidate.layers = layers;
                replay_candidate.buffers = buffers;
                replay_candidate.cutoff_per_mode = cutoff;
                replay_candidate.boundary_leakage =
                    candidate.boundary_leakage;
                return evaluate_replay_candidate(
                    target, replay_candidate, levels, modes, options,
                    replay_selection);
            };
        const snap::PulseResult compiled = snap::pulse_parameter_finder_state(
            padded, layers, cutoff, finder_options);
        Result result;
        result.gate_set = gate_set;
        result.circuit = SnapCircuit{compiled.alphas(layers, cutoff),
                                     compiled.thetas(layers, cutoff)};
        result.error = compiled.err;
        result.converged = compiled.converged;
        result.layers = layers;
        result.buffers = buffers;
        result.cutoff_per_mode = cutoff;
        result.restarts_used = compiled.runs_used;
        result.iterations = compiled.iterations;
        result.objective_evaluations = compiled.objective_evaluations;
        result.boundary_leakage = compiled.boundary_leakage;
        apply_replay_selection(result, replay_selection);
        return result;
    }

    ecd::ECDOptions finder_options =
        ecd_options(options, buffers, cutoff, seed);
    int warm_restarts = 0;
    int warm_iterations = 0;
    int warm_evaluations = 0;
    if (options.ecd.warm_start_layers) {
        const int warm_layers = *options.ecd.warm_start_layers;
        ecd::ECDOptions warm_options = finder_options;
        warm_options.candidate_acceptor = {};
        warm_options.use_initial_guess = false;
        const ecd::ECDParameterFinder warm_finder(levels, modes,
                                                   warm_options);
        const ecd::CompiledCircuit warm =
            warm_finder.attempt_state_parameters(
                target, warm_layers, cutoff,
                configuration_seed(options.seed, warm_layers, buffers));
        const ecd::CompiledCircuit extended =
            extend_ecd_circuit(warm, layers);
        finder_options.use_initial_guess = true;
        finder_options.initial_guess =
            ecd::pack_params(extended.betas, extended.rotations);
        warm_restarts = warm.restarts_used;
        warm_iterations = warm.iterations;
        warm_evaluations = warm.objective_evaluations;
    }
    ReplaySelection replay_selection;
    finder_options.candidate_acceptor =
        [&](const ecd::CompiledCircuit& candidate) {
            Result replay_candidate;
            replay_candidate.gate_set = gate_set;
            replay_candidate.circuit =
                ECDCircuit{candidate.betas, candidate.rotations};
            replay_candidate.error = candidate.err;
            replay_candidate.layers = layers;
            replay_candidate.buffers = buffers;
            replay_candidate.cutoff_per_mode = cutoff;
            replay_candidate.boundary_leakage =
                candidate.boundary_leakage;
            return evaluate_replay_candidate(
                target, replay_candidate, levels, modes, options,
                replay_selection);
        };
    const ecd::ECDParameterFinder finder(levels, modes, finder_options);
    const ecd::CompiledCircuit compiled = finder.attempt_state_parameters(
        target, layers, cutoff, seed);
    Result result;
    result.gate_set = gate_set;
    result.circuit = ECDCircuit{compiled.betas, compiled.rotations};
    result.error = compiled.err;
    result.converged = compiled.accepted;
    result.layers = layers;
    result.buffers = buffers;
    result.cutoff_per_mode = cutoff;
    result.restarts_used = warm_restarts + compiled.restarts_used;
    result.iterations = warm_iterations + compiled.iterations;
    result.objective_evaluations =
        warm_evaluations + compiled.objective_evaluations;
    result.boundary_leakage = compiled.boundary_leakage;
    apply_replay_selection(result, replay_selection);
    return result;
}

double replay_error(const Matrix& target, const Result& compiled, int levels,
                    int modes, int cutoff) {
    if (compiled.gate_set == GateSet::Snap) {
        const auto& circuit = std::get<SnapCircuit>(compiled.circuit);
        const snap::DisplacementBasis basis(cutoff);
        Matrix padded = Matrix::Identity(cutoff, cutoff);
        padded.topLeftCorner(target.rows(), target.cols()) = target;
        return snap::cost_unitary(padded, basis, circuit.alphas,
                                  circuit.thetas, target.rows());
    }
    const auto& circuit = std::get<ECDCircuit>(compiled.circuit);
    const ecd::UnitaryCircuitResult replay = ecd::run_unitary_circuit(
        circuit.betas, circuit.rotations, levels, cutoff);
    const Matrix embedded_target =
        ecd::logical_embedding(levels, modes, cutoff) * target;
    return ecd::unitary_infidelity(embedded_target, replay.ground_block);
}

double replay_error(const Vector& target, const Result& compiled, int levels,
                    int modes, int cutoff) {
    if (compiled.gate_set == GateSet::Snap) {
        const auto& circuit = std::get<SnapCircuit>(compiled.circuit);
        const snap::DisplacementBasis basis(cutoff);
        Vector padded = Vector::Zero(cutoff);
        padded.head(target.size()) = target;
        return snap::cost_state(padded, basis, circuit.alphas, circuit.thetas);
    }
    const auto& circuit = std::get<ECDCircuit>(compiled.circuit);
    const ecd::CircuitResult replay =
        ecd::run_circuit(circuit.betas, circuit.rotations, cutoff);
    const Vector embedded_target =
        ecd::logical_embedding(levels, modes, cutoff) * target;
    return ecd::state_infidelity(embedded_target, replay.psi_g);
}

std::string gate_set_name(GateSet gate_set) {
    return gate_set == GateSet::Snap ? "SNAP" : "ECD";
}

std::string search_failure_message(
    GateSet gate_set, const std::vector<SearchAttempt>& attempts,
    double best_error, double best_replay_error,
    bool had_training_convergence) {
    std::ostringstream message;
    message << gate_set_name(gate_set) << " decomposition failed after ";
    if (attempts.empty()) {
        message << "no attempts";
    } else {
        message << "attempting ";
        for (std::size_t i = 0; i < attempts.size(); ++i) {
            if (i != 0) message << ", ";
            message << "(layers=" << attempts[i].layers
                    << ", buffers=" << attempts[i].buffers << ')';
        }
    }
    message << "; best infidelity=" << best_error;
    if (had_training_convergence) {
        message << "; best replay infidelity=" << best_replay_error;
        message << "; at least one converged circuit failed replay stability";
    }
    return message.str();
}

uint64_t configuration_seed(uint64_t base_seed, int layers, int buffers) {
    const uint64_t layer_bits = static_cast<uint64_t>(layers) << 32U;
    const uint64_t buffer_bits = static_cast<uint64_t>(buffers);
    return base_seed ^ layer_bits ^ buffer_bits;
}

template <typename Target>
Result search(const Target& target, int levels, int modes, GateSet gate_set,
              Preparation preparation, const Options& options) {
    const int logical_dimension = preparation == Preparation::Unitary
                                      ? static_cast<int>(target.rows())
                                      : static_cast<int>(target.size());
    const int heuristic = starting_layers(logical_dimension, levels, modes,
                                          gate_set, preparation);
    const int first_layer = options.layers.value_or(heuristic);
    const int last_layer = maximum_layers(options, heuristic);
    const int first_buffer =
        options.buffers.value_or(automatic_starting_buffers());
    const int last_buffer = options.buffers.value_or(options.max_buffers);
    if (!options.buffers && last_buffer < first_buffer) {
        throw std::invalid_argument(
            "max_buffers is smaller than the automatic starting buffer count");
    }

    std::vector<SearchAttempt> attempts;
    double best_error = std::numeric_limits<double>::infinity();
    double best_replay_error = std::numeric_limits<double>::infinity();
    bool had_training_convergence = false;
    int total_restarts = 0;
    int total_iterations = 0;
    int total_objective_evaluations = 0;
    int carried_layer = first_layer;
    const auto start = std::chrono::steady_clock::now();

    for (int buffers = first_buffer; buffers <= last_buffer; ++buffers) {
        const int layer_begin = options.layers ? first_layer : carried_layer;
        bool converged_at_buffer = false;
        for (int layers = layer_begin; layers <= last_layer; ++layers) {
            attempts.push_back({layers, buffers});
            Result result = compile_at(target, levels, modes, gate_set, layers,
                                       buffers, options,
                                       configuration_seed(options.seed, layers,
                                                          buffers));
            total_restarts += result.restarts_used;
            total_iterations += result.iterations;
            total_objective_evaluations += result.objective_evaluations;
            best_error = std::min(best_error, result.error);
            if (result.error <= options.optimization_threshold) {
                converged_at_buffer = true;
                had_training_convergence = true;
                carried_layer = layers;
                best_replay_error =
                    std::min(best_replay_error, result.replay_error);
            }
            if (!result.converged) continue;

            result.restarts_used = total_restarts;
            result.iterations = total_iterations;
            result.objective_evaluations = total_objective_evaluations;
            result.runtime_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            return result;
        }
        if (!converged_at_buffer) carried_layer = first_layer;
    }
    throw DecompositionError(
        gate_set, attempts, best_error, best_replay_error,
        had_training_convergence);
}

}

DecompositionError::DecompositionError(
    GateSet gate_set, std::vector<SearchAttempt> attempts,
    double best_error, double best_replay_error,
    bool had_training_convergence)
    : std::runtime_error(search_failure_message(
          gate_set, attempts, best_error, best_replay_error,
          had_training_convergence)),
      gate_set_(gate_set),
      attempts_(std::move(attempts)),
      best_error_(best_error),
      best_replay_error_(best_replay_error),
      had_training_convergence_(had_training_convergence) {}

Result decompose(const Matrix& target, int modes, GateSet gate_set,
                 const Options& options) {
    validate_options(options, modes, gate_set);
    if (target.rows() == 0 || target.cols() == 0 ||
        target.rows() != target.cols()) {
        throw std::invalid_argument("unitary target must be nonempty and square");
    }
    const int levels = infer_levels(target.rows(), modes);
    const int dimension = checked_power(levels, modes);
    const Matrix identity = Matrix::Identity(dimension, dimension);
    if (!(target.adjoint() * target).isApprox(identity, 1e-9)) {
        throw std::invalid_argument("unitary target is not unitary");
    }
    return search(target, levels, modes, gate_set, Preparation::Unitary,
                  options);
}

Result decompose(const Vector& target, int modes, GateSet gate_set,
                 const Options& options) {
    validate_options(options, modes, gate_set);
    if (target.size() == 0) {
        throw std::invalid_argument("state target must be nonempty");
    }
    const int levels = infer_levels(target.size(), modes);
    checked_power(levels, modes);
    if (std::abs(target.squaredNorm() - 1.0) > 1e-9) {
        throw std::invalid_argument("state target is not normalized");
    }
    return search(target, levels, modes, gate_set, Preparation::State,
                  options);
}

}
