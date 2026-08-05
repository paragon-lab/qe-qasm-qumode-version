#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include "decomposition.hpp"

namespace {

namespace fs = std::filesystem;

struct Arguments {
    fs::path output_dir = "haar_output";
    int      dimension = 4;
    int      samples = 25;
    int      depth = 8;
    int      buffers = 10;
    int      max_attempts = 0;
    int      max_restarts = 64;
    int      max_iterations = 2000;
    uint64_t seed = 42;
    bool     help = false;
};

struct AttemptRecord {
    int      attempt = 0;
    int      accepted_index = -1;
    uint64_t target_seed = 0;
    uint64_t optimizer_seed = 0;
    std::string status;
    std::string error_message;
    double best_error = std::numeric_limits<double>::quiet_NaN();
    double best_replay_error = std::numeric_limits<double>::quiet_NaN();
    std::optional<decomp::Result> result;
    fs::path target_file;
    fs::path parameter_prefix;
    fs::path replay_file;
};

void print_help(const char* program) {
    std::cout
        << "Compile a deterministic ensemble of Haar-random target states with "
           "ECD, following metriq-qudits.\n\n"
        << "Usage:\n  " << program << " [options]\n\n"
        << "Options:\n"
        << "  --output-dir PATH       Output directory (default: haar_output)\n"
        << "  --dimension N           Qudit dimension (default: 4)\n"
        << "  --samples N             Accepted targets requested (default: 25)\n"
        << "  --depth N               Fixed ECD depth (default: 8)\n"
        << "  --buffers N             Fock buffer levels (default: 10)\n"
        << "  --max-attempts N        Sample cap (default: 2 * samples)\n"
        << "  --max-restarts N        L-BFGS starts per target (default: 64)\n"
        << "  --max-iterations N      Iterations per start (default: 2000)\n"
        << "  --seed N                Ensemble seed (default: 42)\n"
        << "  --help                  Show this help\n";
}

std::string require_value(int& index, int argc, char** argv,
                          std::string_view option) {
    if (++index >= argc) {
        throw std::invalid_argument(std::string(option) + " requires a value");
    }
    return argv[index];
}

uint64_t parse_uint64(const std::string& value, std::string_view option) {
    if (value.empty() || value.front() == '-') {
        throw std::invalid_argument(std::string(option) +
                                    " must be a nonnegative integer");
    }
    std::size_t parsed = 0;
    unsigned long long result = 0;
    try {
        result = std::stoull(value, &parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(option) +
                                    " must be a nonnegative integer");
    }
    if (parsed != value.size()) {
        throw std::invalid_argument(std::string(option) +
                                    " must be a nonnegative integer");
    }
    return static_cast<uint64_t>(result);
}

int parse_int(const std::string& value, std::string_view option) {
    const uint64_t parsed = parse_uint64(value, option);
    if (parsed > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument(std::string(option) + " is too large");
    }
    return static_cast<int>(parsed);
}

Arguments parse_arguments(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help" || option == "-h") {
            arguments.help = true;
        } else if (option == "--output-dir") {
            arguments.output_dir = require_value(index, argc, argv, option);
        } else if (option == "--dimension") {
            arguments.dimension =
                parse_int(require_value(index, argc, argv, option), option);
        } else if (option == "--samples") {
            arguments.samples =
                parse_int(require_value(index, argc, argv, option), option);
        } else if (option == "--depth") {
            arguments.depth =
                parse_int(require_value(index, argc, argv, option), option);
        } else if (option == "--buffers") {
            arguments.buffers =
                parse_int(require_value(index, argc, argv, option), option);
        } else if (option == "--max-attempts") {
            arguments.max_attempts =
                parse_int(require_value(index, argc, argv, option), option);
        } else if (option == "--max-restarts") {
            arguments.max_restarts =
                parse_int(require_value(index, argc, argv, option), option);
        } else if (option == "--max-iterations") {
            arguments.max_iterations =
                parse_int(require_value(index, argc, argv, option), option);
        } else if (option == "--seed") {
            arguments.seed =
                parse_uint64(require_value(index, argc, argv, option), option);
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }

    if (arguments.dimension <= 0) {
        throw std::invalid_argument("--dimension must be positive");
    }
    if (arguments.samples <= 0) {
        throw std::invalid_argument("--samples must be positive");
    }
    if (arguments.max_restarts <= 0 || arguments.max_iterations <= 0) {
        throw std::invalid_argument(
            "--max-restarts and --max-iterations must be positive");
    }
    if (arguments.max_attempts == 0) {
        if (arguments.samples > std::numeric_limits<int>::max() / 2) {
            throw std::invalid_argument("--samples is too large");
        }
        arguments.max_attempts = 2 * arguments.samples;
    }
    if (arguments.max_attempts < arguments.samples) {
        throw std::invalid_argument(
            "--max-attempts must be at least --samples");
    }
    return arguments;
}

uint64_t splitmix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

decomp::Vector haar_state(int dimension, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> normal(0.0, 1.0);
    decomp::Vector target(dimension);
    for (int index = 0; index < dimension; ++index) {
        target(index) = core::Complex(normal(rng), normal(rng));
    }
    target.normalize();
    return target;
}

std::string sample_id(int index) {
    std::ostringstream output;
    output << "sample_" << std::setw(3) << std::setfill('0') << index;
    return output.str();
}

std::ofstream output_file(const fs::path& path) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not open output file: " + path.string());
    }
    output << std::setprecision(17);
    return output;
}

void write_csv_field(std::ostream& output, const std::string& value) {
    output << '"';
    for (const char character : value) {
        if (character == '"') output << '"';
        output << character;
    }
    output << '"';
}

fs::path write_target(const decomp::Vector& target, int attempt,
                      const fs::path& output_dir) {
    const fs::path relative =
        fs::path("targets") / (sample_id(attempt) + ".csv");
    std::ofstream output = output_file(output_dir / relative);
    output << "index,real,imag\n";
    for (Eigen::Index index = 0; index < target.size(); ++index) {
        output << index << ',' << target(index).real() << ','
               << target(index).imag() << '\n';
    }
    return relative;
}

fs::path write_parameters(const decomp::Result& result, int attempt,
                          const fs::path& output_dir) {
    const fs::path prefix =
        fs::path("parameters") / sample_id(attempt);
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
        for (Eigen::Index row = 0; row < circuit.rotations.rows(); ++row) {
            output << row << ',' << circuit.rotations(row, 0) << ','
                   << circuit.rotations(row, 1) << '\n';
        }
    }
    return prefix;
}

fs::path write_replay(const decomp::Result& result, int attempt,
                      const fs::path& output_dir) {
    const fs::path relative =
        fs::path("replay") / (sample_id(attempt) + ".csv");
    std::ofstream output = output_file(output_dir / relative);
    output << "cutoff_per_mode,error\n";
    for (const decomp::ReplayCheck& check : result.replay_checks) {
        output << check.cutoff_per_mode << ',' << check.error << '\n';
    }
    return relative;
}

decomp::Options benchmark_options(const Arguments& arguments,
                                  uint64_t optimizer_seed) {
    decomp::Options options;
    options.layers = arguments.depth;
    options.buffers = arguments.buffers;
    options.max_restarts = arguments.max_restarts;
    options.optimization_threshold = 1e-2;
    options.stability_threshold = 1e-2;
    options.lbfgs_max_iterations = arguments.max_iterations;
    options.seed = optimizer_seed;
    options.ecd.n_penalize =
        arguments.buffers == 0 ? 0 : std::max(1, arguments.buffers / 2);
    options.ecd.penalty_weight = 0.1;
    options.ecd.gradient_method = decomp::GradientMethod::FiniteDifference;
    return options;
}

void write_summary_header(std::ostream& output) {
    output
        << "attempt,accepted_index,target_seed,optimizer_seed,status,"
        << "error_message,dimension,depth,buffers,cutoff_per_mode,"
        << "optimization_error,replay_error,boundary_leakage,restarts_used,"
        << "iterations,objective_evaluations,runtime_seconds,target_file,"
        << "parameter_prefix,replay_file\n";
}

void write_summary_row(std::ostream& output, const AttemptRecord& record,
                       const Arguments& arguments) {
    output << record.attempt << ',';
    if (record.accepted_index >= 0) output << record.accepted_index;
    output << ',' << record.target_seed << ',' << record.optimizer_seed << ',';
    write_csv_field(output, record.status);
    output << ',';
    write_csv_field(output, record.error_message);
    output << ',' << arguments.dimension << ',' << arguments.depth << ','
           << arguments.buffers << ',';
    if (record.result) {
        const decomp::Result& result = *record.result;
        output << result.cutoff_per_mode << ',' << result.error << ','
               << result.replay_error << ',' << result.boundary_leakage << ','
               << result.restarts_used << ',' << result.iterations << ','
               << result.objective_evaluations << ',' << result.runtime_seconds;
    } else {
        output << ',' << record.best_error << ',';
        if (std::isfinite(record.best_replay_error)) {
            output << record.best_replay_error;
        }
        output << ",,,,,";
    }
    output << ',';
    write_csv_field(output, record.target_file.generic_string());
    output << ',';
    write_csv_field(output, record.parameter_prefix.generic_string());
    output << ',';
    write_csv_field(output, record.replay_file.generic_string());
    output << '\n';
}

int run(const Arguments& arguments) {
    fs::create_directories(arguments.output_dir / "targets");
    fs::create_directories(arguments.output_dir / "parameters");
    fs::create_directories(arguments.output_dir / "replay");

    std::ofstream summary = output_file(arguments.output_dir / "summary.csv");
    write_summary_header(summary);

    std::cout
        << "Haar-random state benchmark (metriq-qudits-compatible policy)\n"
        << "  ECD state preparation, d=" << arguments.dimension
        << ", requested=" << arguments.samples
        << ", depth=" << arguments.depth
        << ", buffers=" << arguments.buffers << '\n'
        << "  thresholds: training=1e-2, every replay N+1..N+12=1e-2\n"
        << "  finite-difference gradient, seed=" << arguments.seed << "\n\n";

    int accepted = 0;
    for (int attempt = 0;
         attempt < arguments.max_attempts && accepted < arguments.samples;
         ++attempt) {
        AttemptRecord record;
        record.attempt = attempt;
        record.target_seed =
            splitmix64(arguments.seed ^ static_cast<uint64_t>(2 * attempt));
        record.optimizer_seed = splitmix64(
            arguments.seed ^ static_cast<uint64_t>(2 * attempt + 1));
        const decomp::Vector target =
            haar_state(arguments.dimension, record.target_seed);
        record.target_file =
            write_target(target, attempt, arguments.output_dir);

        std::cout << "[attempt " << attempt + 1 << '/'
                  << arguments.max_attempts << "] ";
        try {
            record.result = decomp::decompose(
                target, 1, decomp::GateSet::Ecd,
                benchmark_options(arguments, record.optimizer_seed));
            record.accepted_index = accepted++;
            record.status = "accepted";
            record.parameter_prefix =
                write_parameters(*record.result, attempt, arguments.output_dir);
            record.replay_file =
                write_replay(*record.result, attempt, arguments.output_dir);
            std::cout << "accepted " << accepted << '/' << arguments.samples
                      << "  err=" << record.result->error
                      << "  replay=" << record.result->replay_error
                      << "  runtime=" << record.result->runtime_seconds
                      << " s\n";
        } catch (const decomp::DecompositionError& error) {
            record.status = error.had_training_convergence()
                                ? "replay_rejected"
                                : "not_converged";
            record.error_message = error.what();
            record.best_error = error.best_error();
            record.best_replay_error = error.best_replay_error();
            std::cout << record.status << "  best=" << record.best_error
                      << '\n';
        }
        write_summary_row(summary, record, arguments);
        summary.flush();
    }

    std::cout << "\nAccepted " << accepted << '/' << arguments.samples
              << " requested targets.\n"
              << "Wrote " << (arguments.output_dir / "summary.csv") << '\n'
              << "Plot with: python3 examples/plot_haar_results.py "
              << arguments.output_dir << '\n';
    return accepted == arguments.samples ? 0 : 1;
}

}  // namespace

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
