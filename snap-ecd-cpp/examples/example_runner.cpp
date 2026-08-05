#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "decomposition.hpp"

namespace {

namespace fs = std::filesystem;

enum class Preparation {
    State,
    Unitary,
};

using Target = std::variant<decomp::Vector, decomp::Matrix>;

struct Example {
    std::string     id;
    std::string     description;
    decomp::GateSet gate_set;
    Preparation    preparation;
    int            modes;
    Target         target;
    decomp::Options options;
    bool           use_default_options = false;
};

struct Arguments {
    fs::path output_dir = "example_output";
    uint64_t seed_offset = 0;
    bool     depth_analysis = false;
    bool     help = false;
};

struct RunRecord {
    const Example* example = nullptr;
    std::optional<decomp::Result> result;
    std::string error_message;
    fs::path target_file;
    fs::path parameter_prefix;
    fs::path replay_file;
    fs::path depth_sweep_file;
};

struct DepthProbe {
    int         depth = 0;
    double      best_error = std::numeric_limits<double>::quiet_NaN();
    bool        accepted = false;
    std::string status;
};

std::string gate_set_name(decomp::GateSet gate_set) {
    return gate_set == decomp::GateSet::Snap ? "SNAP" : "ECD";
}

std::string preparation_name(Preparation preparation) {
    return preparation == Preparation::State ? "state" : "unitary";
}

void print_help(const char* program) {
    std::cout
        << "Run built-in SNAP/ECD decomposition tutorials and write CSV data.\n\n"
        << "Usage:\n  " << program << " [options]\n\n"
        << "Options:\n"
        << "  --output-dir PATH   CSV output directory (default: example_output)\n"
        << "  --seed NUMBER       Offset seeds for explicitly configured examples\n"
        << "  --depth-analysis    Probe minimum stable depth for every unitary\n"
        << "  --help              Show this help\n";
}

std::string require_value(int& index, int argc, char** argv,
                          std::string_view option) {
    if (++index >= argc) {
        throw std::invalid_argument(std::string(option) + " requires a value");
    }
    return argv[index];
}

uint64_t parse_seed(const std::string& value) {
    if (value.empty() || value.front() == '-') {
        throw std::invalid_argument("--seed must be a nonnegative integer");
    }
    std::size_t parsed = 0;
    unsigned long long seed = 0;
    try {
        seed = std::stoull(value, &parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument("--seed must be a nonnegative integer");
    }
    if (parsed != value.size()) {
        throw std::invalid_argument("--seed must be a nonnegative integer");
    }
    return static_cast<uint64_t>(seed);
}

Arguments parse_arguments(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help" || option == "-h") {
            arguments.help = true;
        } else if (option == "--output-dir") {
            arguments.output_dir = require_value(index, argc, argv, option);
            if (arguments.output_dir.empty()) {
                throw std::invalid_argument("--output-dir cannot be empty");
            }
        } else if (option == "--seed") {
            arguments.seed_offset =
                parse_seed(require_value(index, argc, argv, option));
        } else if (option == "--depth-analysis") {
            arguments.depth_analysis = true;
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
    return arguments;
}

decomp::Vector vacuum(int dimension) {
    return decomp::Vector::Unit(dimension, 0);
}

decomp::Matrix identity(int dimension) {
    return decomp::Matrix::Identity(dimension, dimension);
}

decomp::Vector phase_superposition() {
    const double amplitude = 1.0 / std::sqrt(2.0);
    decomp::Vector target(2);
    target << amplitude, core::Complex(0.0, amplitude);
    return target;
}

decomp::Vector coherent_state(double alpha, int dimension) {
    decomp::Vector target(dimension);
    double amplitude = std::exp(-alpha * alpha / 2.0);
    target(0) = amplitude;
    for (int level = 1; level < dimension; ++level) {
        amplitude *= alpha / std::sqrt(static_cast<double>(level));
        target(level) = amplitude;
    }
    target.normalize();
    return target;
}

decomp::Vector phase_tagged_bell_state() {
    const double amplitude = 1.0 / std::sqrt(2.0);
    decomp::Vector target = decomp::Vector::Zero(4);
    target(1) = amplitude;
    target(2) = core::Complex(0.0, amplitude);
    return target;
}

decomp::Matrix pauli_x() {
    decomp::Matrix target(2, 2);
    target << 0.0, 1.0,
              1.0, 0.0;
    return target;
}

decomp::Matrix pauli_z() {
    decomp::Matrix target(2, 2);
    target << 1.0,  0.0,
              0.0, -1.0;
    return target;
}

decomp::Matrix fourier_gate(int dimension) {
    const double scale = 1.0 / std::sqrt(static_cast<double>(dimension));
    const double two_pi = 2.0 * std::acos(-1.0);
    decomp::Matrix target(dimension, dimension);
    for (int row = 0; row < dimension; ++row) {
        for (int column = 0; column < dimension; ++column) {
            const double angle = two_pi * row * column / dimension;
            target(row, column) =
                scale * std::exp(core::Complex(0.0, angle));
        }
    }
    return target;
}

decomp::Options fixed_options(int layers, int buffers, uint64_t seed) {
    decomp::Options options;
    options.layers = layers;
    options.buffers = buffers;
    options.max_restarts = 5;
    options.optimization_threshold = 1e-3;
    options.stability_threshold = 2e-3;
    options.lbfgs_max_iterations = 3000;
    options.seed = seed;
    options.ecd.gradient_method = decomp::GradientMethod::FiniteDifference;
    return options;
}

std::vector<Example> make_examples(uint64_t seed_offset) {
    if (seed_offset > std::numeric_limits<uint64_t>::max() - 2903) {
        throw std::invalid_argument("--seed is too large");
    }
    std::vector<Example> examples;

    decomp::Options snap_phase_options =
        fixed_options(1, 2, seed_offset + 101);
    snap_phase_options.optimization_threshold = 1e-6;
    snap_phase_options.stability_threshold = 1e-6;

    decomp::Options snap_state_options =
        fixed_options(0, 2, seed_offset + 211);
    snap_state_options.max_restarts = 1;
    snap_state_options.optimization_threshold = 1e-9;
    snap_state_options.stability_threshold = 1e-9;
    snap_state_options.lbfgs_tolerance = 1e-10;

    decomp::Options ecd_state_options =
        fixed_options(4, 2, seed_offset + 1701);
    ecd_state_options.optimization_threshold = 5e-4;
    ecd_state_options.stability_threshold = 5e-3;
    ecd_state_options.ecd.n_penalize = 2;
    ecd_state_options.ecd.penalty_weight = 0.1;

    decomp::Options ecd_unitary_options =
        fixed_options(8, 2, seed_offset + 2903);
    ecd_unitary_options.optimization_threshold = 5e-4;
    ecd_unitary_options.stability_threshold = 6e-3;
    ecd_unitary_options.ecd.n_penalize = 2;
    ecd_unitary_options.ecd.penalty_weight = 0.1;

    decomp::Options bell_options =
        fixed_options(4, 3, seed_offset + 3);
    bell_options.max_restarts = 1;
    bell_options.lbfgs_max_iterations = 200;
    bell_options.optimization_threshold = 1e-2;
    bell_options.stability_threshold = 1.2e-2;
    bell_options.ecd.n_penalize = 0;
    bell_options.ecd.penalty_weight = 0.0;

    decomp::Options two_mode_identity_options =
        fixed_options(0, 0, seed_offset + 401);
    two_mode_identity_options.max_restarts = 1;
    two_mode_identity_options.optimization_threshold = 1e-8;
    two_mode_identity_options.stability_threshold = 1e-8;
    two_mode_identity_options.ecd.n_penalize = 0;
    two_mode_identity_options.ecd.penalty_weight = 0.0;

    decomp::Options snap_fourier_options =
        fixed_options(6, 10, seed_offset + 404);
    snap_fourier_options.max_restarts = 5;
    snap_fourier_options.stability_threshold = 1e-2;
    snap_fourier_options.snap.n_penalize = 5;
    snap_fourier_options.snap.penalty_weight = 0.1;

    decomp::Options ecd_fourier_options =
        fixed_options(28, 10, seed_offset + 804);
    ecd_fourier_options.max_restarts = 1;
    ecd_fourier_options.lbfgs_tolerance = 1e-8;
    ecd_fourier_options.lbfgs_max_iterations = 2500;
    ecd_fourier_options.optimization_threshold = 1e-3;
    ecd_fourier_options.stability_threshold = 1e-2;
    ecd_fourier_options.ecd.n_penalize = 5;
    ecd_fourier_options.ecd.penalty_weight = 0.01;
    ecd_fourier_options.ecd.gradient_method =
        decomp::GradientMethod::FiniteDifference;
    ecd_fourier_options.ecd.warm_start_layers = 24;

    examples.push_back({
        "snap_identity_auto",
        "SNAP unitary decomposition with automatic layer and buffer sizing",
        decomp::GateSet::Snap,
        Preparation::Unitary,
        1,
        identity(2),
        {},
        true,
    });
    examples.push_back({
        "ecd_vacuum_auto",
        "ECD state preparation with automatic layer and buffer sizing",
        decomp::GateSet::Ecd,
        Preparation::State,
        1,
        vacuum(2),
        {},
        true,
    });
    examples.push_back({
        "snap_pauli_z",
        "SNAP synthesis of a nontrivial single-mode phase gate",
        decomp::GateSet::Snap,
        Preparation::Unitary,
        1,
        pauli_z(),
        snap_phase_options,
    });
    examples.push_back({
        "snap_coherent_state",
        "SNAP preparation of a truncated coherent state with alpha=0.42",
        decomp::GateSet::Snap,
        Preparation::State,
        1,
        coherent_state(0.42, 10),
        snap_state_options,
    });
    examples.push_back({
        "ecd_pauli_x",
        "ECD synthesis of the single-mode Pauli-X unitary",
        decomp::GateSet::Ecd,
        Preparation::Unitary,
        1,
        pauli_x(),
        ecd_unitary_options,
    });
    examples.push_back({
        "ecd_phase_superposition",
        "ECD preparation of (|0> + i|1>)/sqrt(2)",
        decomp::GateSet::Ecd,
        Preparation::State,
        1,
        phase_superposition(),
        ecd_state_options,
    });
    examples.push_back({
        "ecd_phase_tagged_bell",
        "ECD preparation of a phase-tagged two-mode Bell state",
        decomp::GateSet::Ecd,
        Preparation::State,
        2,
        phase_tagged_bell_state(),
        bell_options,
    });
    examples.push_back({
        "ecd_two_mode_identity",
        "ECD decomposition of a two-mode identity unitary",
        decomp::GateSet::Ecd,
        Preparation::Unitary,
        2,
        identity(4),
        two_mode_identity_options,
    });
    examples.push_back({
        "snap_fourier_d4",
        "SNAP decomposition of the dimension-4 Fourier gate",
        decomp::GateSet::Snap,
        Preparation::Unitary,
        1,
        fourier_gate(4),
        snap_fourier_options,
    });
    examples.push_back({
        "ecd_fourier_d4",
        "ECD decomposition of the dimension-4 Fourier gate",
        decomp::GateSet::Ecd,
        Preparation::Unitary,
        1,
        fourier_gate(4),
        ecd_fourier_options,
    });
    return examples;
}

std::ofstream output_file(const fs::path& path) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not open output file: " + path.string());
    }
    output << std::setprecision(17);
    return output;
}

fs::path write_target(const Example& example, const fs::path& output_dir) {
    const fs::path relative = fs::path("targets") / (example.id + ".csv");
    std::ofstream output = output_file(output_dir / relative);
    if (example.preparation == Preparation::State) {
        output << "index,real,imag\n";
        const auto& target = std::get<decomp::Vector>(example.target);
        for (Eigen::Index index = 0; index < target.size(); ++index) {
            output << index << ',' << target(index).real() << ','
                   << target(index).imag() << '\n';
        }
    } else {
        output << "row,column,real,imag\n";
        const auto& target = std::get<decomp::Matrix>(example.target);
        for (Eigen::Index row = 0; row < target.rows(); ++row) {
            for (Eigen::Index column = 0; column < target.cols(); ++column) {
                output << row << ',' << column << ','
                       << target(row, column).real() << ','
                       << target(row, column).imag() << '\n';
            }
        }
    }
    return relative;
}

fs::path write_parameters(const Example& example,
                          const decomp::Result& result,
                          const fs::path& output_dir) {
    const fs::path prefix = fs::path("parameters") / example.id;
    if (result.gate_set == decomp::GateSet::Snap) {
        const auto& circuit = std::get<decomp::SnapCircuit>(result.circuit);
        {
            std::ofstream output =
                output_file(output_dir / (prefix.string() + "_alphas.csv"));
            output << "index,value\n";
            for (Eigen::Index index = 0; index < circuit.alphas.size(); ++index) {
                output << index << ',' << circuit.alphas(index) << '\n';
            }
        }
        {
            std::ofstream output =
                output_file(output_dir / (prefix.string() + "_thetas.csv"));
            output << "layer,level,phase\n";
            for (Eigen::Index layer = 0; layer < circuit.thetas.rows(); ++layer) {
                for (Eigen::Index level = 0; level < circuit.thetas.cols(); ++level) {
                    output << layer << ',' << level << ','
                           << circuit.thetas(layer, level) << '\n';
                }
            }
        }
    } else {
        const auto& circuit = std::get<decomp::ECDCircuit>(result.circuit);
        {
            std::ofstream output =
                output_file(output_dir / (prefix.string() + "_betas.csv"));
            output << "layer,mode,real,imag\n";
            for (Eigen::Index layer = 0; layer < circuit.betas.rows(); ++layer) {
                for (Eigen::Index mode = 0; mode < circuit.betas.cols(); ++mode) {
                    output << layer << ',' << mode << ','
                           << circuit.betas(layer, mode).real() << ','
                           << circuit.betas(layer, mode).imag() << '\n';
                }
            }
        }
        {
            std::ofstream output =
                output_file(output_dir / (prefix.string() + "_rotations.csv"));
            output << "rotation,theta,phi\n";
            for (Eigen::Index rotation = 0;
                 rotation < circuit.rotations.rows(); ++rotation) {
                output << rotation << ',' << circuit.rotations(rotation, 0)
                       << ',' << circuit.rotations(rotation, 1) << '\n';
            }
        }
    }
    return prefix;
}

fs::path write_replay_curve(const Example& example,
                            const decomp::Result& result,
                            const fs::path& output_dir) {
    const fs::path relative =
        fs::path("replay") / (example.id + ".csv");
    std::ofstream output = output_file(output_dir / relative);
    output << "cutoff_per_mode,error\n";
    for (const decomp::ReplayCheck& check : result.replay_checks) {
        output << check.cutoff_per_mode << ',' << check.error << '\n';
    }
    return relative;
}

int logical_dimension(const Example& example);
void write_csv_field(std::ostream& output, const std::string& value);

long long integer_power(int base, int exponent) {
    long long result = 1;
    for (int index = 0; index < exponent; ++index) result *= base;
    return result;
}

bool is_identity_target(const Example& example) {
    if (example.preparation != Preparation::Unitary) return false;
    const decomp::Matrix& target = std::get<decomp::Matrix>(example.target);
    return target.isApprox(
        decomp::Matrix::Identity(target.rows(), target.cols()), 1e-12);
}

int parameter_counting_floor(const Example& example) {
    if (is_identity_target(example)) return 0;
    const long long dimension = logical_dimension(example);
    int levels = 1;
    while (integer_power(levels, example.modes) < dimension) ++levels;

    const long long remaining =
        dimension * dimension -
        (example.gate_set == decomp::GateSet::Snap ? 2LL : 3LL);
    const long long per_layer =
        example.gate_set == decomp::GateSet::Snap
            ? levels + 1LL
            : 4LL * example.modes;
    return static_cast<int>(
        std::max(0LL, (remaining + per_layer - 1) / per_layer));
}

std::vector<DepthProbe> probe_depths(const Example& example,
                                     const decomp::Result& accepted_result) {
    std::vector<DepthProbe> probes;
    const int first_depth = parameter_counting_floor(example);
    for (int depth = first_depth; depth <= accepted_result.layers; ++depth) {
        if (depth == accepted_result.layers) {
            probes.push_back({depth, accepted_result.error, true, "accepted"});
            break;
        }

        decomp::Options options =
            example.use_default_options ? decomp::Options{} : example.options;
        options.layers = depth;
        options.buffers = accepted_result.buffers;
        options.max_restarts = 1;
        options.lbfgs_max_iterations =
            std::min(options.lbfgs_max_iterations, 1000);
        options.ecd.warm_start_layers.reset();

        try {
            const decomp::Result result = std::visit(
                [&](const auto& target) {
                    return decomp::decompose(target, example.modes,
                                             example.gate_set, options);
                },
                example.target);
            probes.push_back({depth, result.error, true, "accepted"});
            break;
        } catch (const decomp::DecompositionError& error) {
            probes.push_back({
                depth,
                error.best_error(),
                false,
                error.had_training_convergence() ? "replay_rejected"
                                                 : "not_converged",
            });
        }
    }
    return probes;
}

fs::path write_depth_sweep(const Example& example,
                           const decomp::Result& result,
                           const fs::path& output_dir) {
    const fs::path relative =
        fs::path("analysis/depth") / (example.id + ".csv");
    std::ofstream output = output_file(output_dir / relative);
    output << "depth,best_optimization_error,accepted,status\n";
    for (const DepthProbe& probe : probe_depths(example, result)) {
        output << probe.depth << ',';
        if (std::isfinite(probe.best_error)) output << probe.best_error;
        output << ',' << (probe.accepted ? "true" : "false") << ',';
        write_csv_field(output, probe.status);
        output << '\n';
    }
    return relative;
}

decomp::Result run_decomposition(const Example& example) {
    return std::visit(
        [&](const auto& target) {
            if (example.use_default_options) {
                return decomp::decompose(target, example.modes,
                                         example.gate_set);
            }
            return decomp::decompose(target, example.modes, example.gate_set,
                                     example.options);
        },
        example.target);
}

void write_csv_field(std::ostream& output, const std::string& value) {
    output << '"';
    for (const char character : value) {
        if (character == '"') output << '"';
        output << character;
    }
    output << '"';
}

int logical_dimension(const Example& example) {
    return std::visit(
        [](const auto& target) {
            if constexpr (std::is_same_v<std::decay_t<decltype(target)>,
                                         decomp::Vector>) {
                return static_cast<int>(target.size());
            } else {
                return static_cast<int>(target.rows());
            }
        },
        example.target);
}

void write_summary_header(std::ostream& output) {
    output
        << "example_id,description,gate_set,target_kind,modes,logical_dimension,"
        << "seed,status,error_message,layers,buffers,cutoff_per_mode,"
        << "optimization_error,replay_error,converged,restarts_used,iterations,"
        << "objective_evaluations,boundary_leakage,runtime_seconds,target_file,"
        << "parameter_prefix,replay_file,depth_sweep_file\n";
}

void write_summary_row(std::ostream& output, const RunRecord& record) {
    const Example& example = *record.example;
    write_csv_field(output, example.id);
    output << ',';
    write_csv_field(output, example.description);
    output << ',' << gate_set_name(example.gate_set) << ','
           << preparation_name(example.preparation) << ',' << example.modes
           << ',' << logical_dimension(example) << ',';
    if (example.use_default_options) {
        output << "default";
    } else {
        output << example.options.seed;
    }
    output << ',' << (record.result ? "success" : "failed") << ',';
    write_csv_field(output, record.error_message);
    output << ',';
    if (record.result) {
        const decomp::Result& result = *record.result;
        output << result.layers << ',' << result.buffers << ','
               << result.cutoff_per_mode << ',' << result.error << ','
               << result.replay_error << ','
               << (result.converged ? "true" : "false") << ','
               << result.restarts_used << ',' << result.iterations << ','
               << result.objective_evaluations << ',';
        if (std::isfinite(result.boundary_leakage)) {
            output << result.boundary_leakage;
        }
        output << ',' << result.runtime_seconds;
    } else {
        output << ",,,,,,,,,,";
    }
    output << ',';
    write_csv_field(output, record.target_file.generic_string());
    output << ',';
    write_csv_field(output, record.parameter_prefix.generic_string());
    output << ',';
    write_csv_field(output, record.replay_file.generic_string());
    output << ',';
    write_csv_field(output, record.depth_sweep_file.generic_string());
    output << '\n';
}

void print_result(const decomp::Result& result) {
    std::cout << "    layers=" << result.layers
              << ", buffers=" << result.buffers
              << ", cutoff=" << result.cutoff_per_mode << '\n'
              << "    optimization error=" << result.error
              << ", replay error=" << result.replay_error << '\n'
              << "    restarts=" << result.restarts_used
              << ", iterations=" << result.iterations
              << ", objective evaluations=" << result.objective_evaluations
              << ", runtime=" << result.runtime_seconds << " s\n";
}

int run(const Arguments& arguments) {
    fs::create_directories(arguments.output_dir / "targets");
    fs::create_directories(arguments.output_dir / "parameters");
    fs::create_directories(arguments.output_dir / "replay");
    if (arguments.depth_analysis) {
        fs::create_directories(arguments.output_dir / "analysis/depth");
    }

    std::ofstream summary = output_file(arguments.output_dir / "summary.csv");
    write_summary_header(summary);

    const std::vector<Example> all_examples =
        make_examples(arguments.seed_offset);

    std::cout << "snap-ecd-cpp public API tutorial\n"
              << "  Matrix target -> unitary decomposition overload\n"
              << "  Vector target -> state preparation overload\n"
              << "  Result::circuit -> std::variant<SnapCircuit, ECDCircuit>\n\n";

    int failures = 0;
    for (std::size_t index = 0; index < all_examples.size(); ++index) {
        const Example& example = all_examples[index];
        std::cout << '[' << index + 1 << '/' << all_examples.size() << "] "
                  << example.id << '\n'
                  << "    " << example.description << '\n'
                  << "    decomp::decompose(" << preparation_name(example.preparation)
                  << " target, modes=" << example.modes << ", GateSet::"
                  << (example.gate_set == decomp::GateSet::Snap ? "Snap" : "Ecd")
                  << (example.use_default_options ? ")\n" : ", options)\n");

        RunRecord record;
        record.example = &example;
        record.target_file = write_target(example, arguments.output_dir);
        try {
            record.result = run_decomposition(example);
            record.parameter_prefix =
                write_parameters(example, *record.result, arguments.output_dir);
            record.replay_file =
                write_replay_curve(example, *record.result,
                                   arguments.output_dir);
            if (arguments.depth_analysis &&
                example.preparation == Preparation::Unitary) {
                std::cout << "    probing minimum stable depth (one restart, "
                             "up to 1000 iterations per depth)\n";
                record.depth_sweep_file =
                    write_depth_sweep(example, *record.result,
                                      arguments.output_dir);
            }
            print_result(*record.result);
        } catch (const std::exception& error) {
            ++failures;
            record.error_message = error.what();
            std::cout << "    FAILED: " << error.what() << '\n';
        }
        write_summary_row(summary, record);
        summary.flush();
        std::cout << '\n';
    }

    std::cout << "Wrote " << (arguments.output_dir / "summary.csv") << '\n'
              << "Plot with: python3 examples/plot_results.py "
              << arguments.output_dir << '\n';
    if (failures != 0) {
        std::cerr << failures << " example(s) failed; successful runs were still saved.\n";
        return 1;
    }
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        if (arguments.help) {
            print_help(argv[0]);
            return 0;
        }
        return run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n\n";
        print_help(argv[0]);
        return 2;
    }
}
