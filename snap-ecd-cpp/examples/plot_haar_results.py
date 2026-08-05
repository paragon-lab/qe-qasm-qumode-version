#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def parse_args():
    parser = argparse.ArgumentParser(
        description="Plot the C++ Haar-state benchmark and optionally compare metriq-qudits."
    )
    parser.add_argument(
        "output_dir",
        nargs="?",
        default="haar_output",
        type=Path,
        help="benchmark output directory (default: haar_output)",
    )
    parser.add_argument(
        "--metriq-baseline",
        type=Path,
        help="optional metriq-qudits compiled-circuit .npz with matching settings",
    )
    parser.add_argument("--show", action="store_true", help="open plot windows")
    return parser.parse_args()


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def finite_values(rows, key):
    return np.asarray(
        [float(row[key]) for row in rows if row.get(key, "")], dtype=float
    )


def load_metriq(path):
    if path is None:
        return None
    data = np.load(path, allow_pickle=False)
    return {
        "path": path,
        "errors": np.asarray(data["err"], dtype=float),
        "boundary": np.asarray(data["boundary_leakage"], dtype=float),
        "depths": np.asarray(data["k_per_circuit"], dtype=int),
        "accepted": int(data["N_unitaries"].item()),
        "attempted": int(data["n_attempted"].item()),
        "dimension": int(data["d"].item()),
        "cutoff": int(data["N_cav"].item()),
    }


def bin_edges(*arrays, logarithmic=False):
    values = np.concatenate([array for array in arrays if array.size])
    count = min(16, max(6, int(np.sqrt(values.size))))
    low = float(values.min())
    high = float(values.max())
    if low == high:
        low *= 0.9
        high *= 1.1
    if logarithmic:
        return np.geomspace(low, high, count + 1)
    return np.linspace(low, high, count + 1)


def plot_distribution(axis, cpp, baseline, title, xlabel, logarithmic=False):
    arrays = [cpp]
    if baseline is not None:
        arrays.append(baseline)
    bins = bin_edges(*arrays, logarithmic=logarithmic)
    axis.hist(cpp, bins=bins, alpha=0.65, color="#F58518", label="C++ finite difference")
    if baseline is not None:
        axis.hist(baseline, bins=bins, alpha=0.5, color="#4C78A8", label="metriq-qudits")
    axis.set_title(title)
    axis.set_xlabel(xlabel)
    axis.set_ylabel("Circuits")
    if logarithmic:
        axis.set_xscale("log")
    axis.grid(axis="y", alpha=0.25)
    axis.legend(fontsize="small")


def plot_ensemble(rows, baseline, output_dir):
    accepted = [row for row in rows if row["status"] == "accepted"]
    errors = finite_values(accepted, "optimization_error")
    replay = finite_values(accepted, "replay_error")
    boundary = finite_values(accepted, "boundary_leakage")
    runtimes = finite_values(accepted, "runtime_seconds")

    figure, axes = plt.subplots(2, 2, figsize=(13, 9), constrained_layout=True)
    plot_distribution(
        axes[0, 0],
        errors,
        None if baseline is None else baseline["errors"],
        "Accepted optimization infidelity",
        "Infidelity",
        logarithmic=True,
    )
    axes[0, 0].axvline(1e-2, color="#E45756", linestyle="--", label="limit 1e-2")

    plot_distribution(
        axes[0, 1],
        boundary,
        None if baseline is None else baseline["boundary"],
        "Maximum boundary leakage",
        "Population",
        logarithmic=True,
    )

    axes[1, 0].hist(
        replay,
        bins=bin_edges(replay, logarithmic=True),
        color="#54A24B",
        alpha=0.8,
    )
    axes[1, 0].axvline(1e-2, color="#E45756", linestyle="--", label="limit 1e-2")
    axes[1, 0].set_title("Worst error over N+1 … N+12")
    axes[1, 0].set_xlabel("Worst replay infidelity")
    axes[1, 0].set_ylabel("Circuits")
    axes[1, 0].set_xscale("log")
    axes[1, 0].grid(axis="y", alpha=0.25)
    axes[1, 0].legend(fontsize="small")

    axes[1, 1].hist(runtimes, bins=bin_edges(runtimes), color="#B279A2", alpha=0.8)
    axes[1, 1].set_title("C++ decomposition runtime")
    axes[1, 1].set_xlabel("Seconds per accepted target")
    axes[1, 1].set_ylabel("Circuits")
    axes[1, 1].grid(axis="y", alpha=0.25)

    path = output_dir / "ensemble_comparison.png"
    figure.savefig(path, dpi=180)
    return figure, path


def plot_replays(rows, output_dir):
    figure, axis = plt.subplots(figsize=(10, 6), constrained_layout=True)
    for row in rows:
        if row["status"] != "accepted":
            continue
        checks = read_csv(output_dir / row["replay_file"])
        axis.semilogy(
            [int(check["cutoff_per_mode"]) for check in checks],
            [max(float(check["error"]), 1e-16) for check in checks],
            "-o",
            markersize=2.5,
            linewidth=0.9,
            alpha=0.45,
            color="#4C78A8",
        )
    axis.axhline(1e-2, color="#E45756", linestyle="--", label="stability limit (1e-2)")
    axis.set_xlabel("Per-mode replay cutoff")
    axis.set_ylabel("Infidelity")
    axis.set_title("Haar-state cutoff stability")
    axis.xaxis.set_major_locator(plt.MaxNLocator(integer=True))
    axis.grid(True, which="both", alpha=0.25)
    axis.legend()
    path = output_dir / "ensemble_replay_curves.png"
    figure.savefig(path, dpi=180)
    return figure, path


def stats(source, accepted, attempted, errors, boundary, depths, replay=None):
    def value(array, reducer):
        return "" if array is None or not array.size else f"{reducer(array):.17g}"

    return {
        "source": source,
        "accepted": accepted,
        "attempted": attempted,
        "success_rate": accepted / attempted if attempted else 0.0,
        "depth_min": value(depths, np.min),
        "depth_median": value(depths, np.median),
        "depth_max": value(depths, np.max),
        "error_min": value(errors, np.min),
        "error_median": value(errors, np.median),
        "error_max": value(errors, np.max),
        "replay_median": value(replay, np.median),
        "replay_max": value(replay, np.max),
        "boundary_median": value(boundary, np.median),
        "boundary_max": value(boundary, np.max),
    }


def write_comparison(rows, baseline, output_dir):
    accepted_rows = [row for row in rows if row["status"] == "accepted"]
    attempted = len(rows)
    cpp = stats(
        "snap-ecd-cpp",
        len(accepted_rows),
        attempted,
        finite_values(accepted_rows, "optimization_error"),
        finite_values(accepted_rows, "boundary_leakage"),
        finite_values(accepted_rows, "depth"),
        finite_values(accepted_rows, "replay_error"),
    )
    records = [cpp]
    if baseline is not None:
        records.append(
            stats(
                "metriq-qudits",
                baseline["accepted"],
                baseline["attempted"],
                baseline["errors"],
                baseline["boundary"],
                baseline["depths"],
            )
        )
    path = output_dir / "comparison.csv"
    with path.open("w", newline="", encoding="utf-8") as destination:
        writer = csv.DictWriter(destination, fieldnames=list(cpp))
        writer.writeheader()
        writer.writerows(records)
    return path


def main():
    args = parse_args()
    summary_path = args.output_dir / "summary.csv"
    if not summary_path.is_file():
        raise SystemExit(f"summary file not found: {summary_path}")
    if args.metriq_baseline is not None and not args.metriq_baseline.is_file():
        raise SystemExit(f"metriq baseline not found: {args.metriq_baseline}")

    rows = read_csv(summary_path)
    accepted = [row for row in rows if row["status"] == "accepted"]
    if not accepted:
        raise SystemExit(f"no accepted circuits found in {summary_path}")
    baseline = load_metriq(args.metriq_baseline)
    if baseline is not None:
        dimension = int(accepted[0]["dimension"])
        cutoff = int(accepted[0]["cutoff_per_mode"])
        if baseline["dimension"] != dimension or baseline["cutoff"] != cutoff:
            raise SystemExit(
                "baseline mismatch: expected matching dimension and optimization cutoff"
            )

    figures_and_paths = [
        plot_ensemble(rows, baseline, args.output_dir),
        plot_replays(rows, args.output_dir),
    ]
    comparison_path = write_comparison(rows, baseline, args.output_dir)
    for _, path in figures_and_paths:
        print(f"Wrote {path}")
    print(f"Wrote {comparison_path}")

    if args.show:
        plt.show()
    else:
        for figure, _ in figures_and_paths:
            plt.close(figure)


if __name__ == "__main__":
    main()
