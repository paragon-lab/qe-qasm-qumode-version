#!/usr/bin/env python3

import argparse
import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt


def parse_args():
    parser = argparse.ArgumentParser(
        description="Plot CSV output from the snap_ecd_examples runner."
    )
    parser.add_argument(
        "output_dir",
        nargs="?",
        default="example_output",
        type=Path,
        help="runner output directory (default: example_output)",
    )
    parser.add_argument("--show", action="store_true", help="open plot windows")
    return parser.parse_args()


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def number(row, key, default=0.0):
    value = row.get(key, "")
    return float(value) if value else default


def colors(rows):
    return ["#4C78A8" if row["gate_set"] == "SNAP" else "#F58518" for row in rows]


def finish_axis(axis, labels):
    axis.set_xticks(range(len(labels)), labels, rotation=35, ha="right")
    axis.grid(axis="y", alpha=0.25)


def plot_metrics(rows, output_dir):
    labels = [row["example_id"] for row in rows]
    positions = list(range(len(rows)))
    figure, axes = plt.subplots(2, 2, figsize=(15, 10), constrained_layout=True)

    width = 0.38
    optimization = [max(number(row, "optimization_error"), 1e-16) for row in rows]
    replay = [max(number(row, "replay_error"), 1e-16) for row in rows]
    axes[0, 0].bar([x - width / 2 for x in positions], optimization, width, label="optimization")
    axes[0, 0].bar([x + width / 2 for x in positions], replay, width, label="replay")
    axes[0, 0].set_yscale("log")
    axes[0, 0].set_ylabel("Infidelity")
    axes[0, 0].set_title("Optimization and replay error")
    axes[0, 0].legend()
    finish_axis(axes[0, 0], labels)

    axes[0, 1].bar(positions, [number(row, "runtime_seconds") for row in rows], color=colors(rows))
    axes[0, 1].set_ylabel("Seconds")
    axes[0, 1].set_title("Runtime (blue=SNAP, orange=ECD)")
    finish_axis(axes[0, 1], labels)

    axes[1, 0].bar(
        [x - width / 2 for x in positions],
        [number(row, "layers") for row in rows],
        width,
        label="layers",
    )
    axes[1, 0].bar(
        [x + width / 2 for x in positions],
        [number(row, "buffers") for row in rows],
        width,
        label="buffers",
    )
    axes[1, 0].set_ylabel("Count")
    axes[1, 0].set_title("Selected circuit sizes")
    axes[1, 0].legend()
    finish_axis(axes[1, 0], labels)

    iterations = [max(number(row, "iterations"), 1.0) for row in rows]
    evaluations = [max(number(row, "objective_evaluations"), 1.0) for row in rows]
    axes[1, 1].bar([x - width / 2 for x in positions], iterations, width, label="iterations")
    axes[1, 1].bar([x + width / 2 for x in positions], evaluations, width, label="evaluations")
    axes[1, 1].set_yscale("log")
    axes[1, 1].set_ylabel("Count (log scale)")
    axes[1, 1].set_title("Optimizer work")
    axes[1, 1].legend()
    finish_axis(axes[1, 1], labels)

    path = output_dir / "summary_plots.png"
    figure.savefig(path, dpi=180)
    return figure, path


def collect_parameter_values(rows, output_dir):
    values = {"alphas": [], "thetas": [], "beta_magnitudes": [], "theta": [], "phi": []}
    for row in rows:
        prefix = output_dir / row["parameter_prefix"]
        if row["gate_set"] == "SNAP":
            for item in read_rows(Path(f"{prefix}_alphas.csv")):
                values["alphas"].append(float(item["value"]))
            for item in read_rows(Path(f"{prefix}_thetas.csv")):
                values["thetas"].append(float(item["phase"]))
        else:
            for item in read_rows(Path(f"{prefix}_betas.csv")):
                real = float(item["real"])
                imag = float(item["imag"])
                values["beta_magnitudes"].append(math.hypot(real, imag))
            for item in read_rows(Path(f"{prefix}_rotations.csv")):
                values["theta"].append(float(item["theta"]))
                values["phi"].append(float(item["phi"]))
    return values


def histogram(axis, values, title, label, color):
    if values:
        axis.hist(
            values,
            bins=min(20, max(5, int(math.sqrt(len(values))))),
            color=color,
            alpha=0.85,
        )
    else:
        axis.text(
            0.5,
            0.5,
            "No parameters in this suite",
            ha="center",
            va="center",
            transform=axis.transAxes,
        )
    axis.set_title(title)
    axis.set_xlabel(label)
    axis.set_ylabel("Count")
    axis.grid(axis="y", alpha=0.25)


def plot_parameters(rows, output_dir):
    values = collect_parameter_values(rows, output_dir)
    figure, axes = plt.subplots(2, 2, figsize=(12, 8), constrained_layout=True)
    histogram(axes[0, 0], values["alphas"], "SNAP displacement parameters", "alpha", "#4C78A8")
    histogram(axes[0, 1], values["thetas"], "SNAP phase parameters", "theta", "#72B7B2")
    histogram(
        axes[1, 0],
        values["beta_magnitudes"],
        "ECD displacement magnitudes",
        "|beta|",
        "#F58518",
    )

    if values["theta"] or values["phi"]:
        axes[1, 1].hist(values["theta"], bins=15, alpha=0.7, label="theta", color="#E45756")
        axes[1, 1].hist(values["phi"], bins=15, alpha=0.7, label="phi", color="#B279A2")
        axes[1, 1].legend()
    else:
        axes[1, 1].text(
            0.5,
            0.5,
            "No parameters in this suite",
            ha="center",
            va="center",
            transform=axes[1, 1].transAxes,
        )
    axes[1, 1].set_title("ECD rotation parameters")
    axes[1, 1].set_xlabel("Angle")
    axes[1, 1].set_ylabel("Count")
    axes[1, 1].grid(axis="y", alpha=0.25)

    path = output_dir / "parameter_distributions.png"
    figure.savefig(path, dpi=180)
    return figure, path


def plot_replay_curves(rows, output_dir):
    figure, axis = plt.subplots(figsize=(12, 7), constrained_layout=True)
    for row in rows:
        replay_path = output_dir / row["replay_file"]
        checks = read_rows(replay_path)
        cutoffs = [int(check["cutoff_per_mode"]) for check in checks]
        errors = [max(float(check["error"]), 1e-16) for check in checks]
        color = "#4C78A8" if row["gate_set"] == "SNAP" else "#F58518"
        axis.plot(
            cutoffs,
            errors,
            marker="o",
            markersize=3,
            linewidth=1.4,
            label=row["example_id"],
            color=color,
            alpha=0.8,
        )
    axis.axhline(1e-2, color="#E45756", linestyle="--", label="stability limit (1e-2)")
    axis.set_yscale("log")
    axis.set_xlabel("Per-mode replay cutoff")
    axis.set_ylabel("Infidelity")
    axis.set_title("Cutoff-stability replay curves")
    axis.grid(alpha=0.25)
    axis.legend(fontsize="small", ncols=2)

    path = output_dir / "replay_stability.png"
    figure.savefig(path, dpi=180)
    return figure, path


def plot_circuit_replays(rows, output_dir):
    plot_dir = output_dir / "circuit_replay"
    plot_dir.mkdir(parents=True, exist_ok=True)
    figures_and_paths = []
    for row in rows:
        checks = read_rows(output_dir / row["replay_file"])
        cutoffs = [int(row["cutoff_per_mode"])] + [
            int(check["cutoff_per_mode"]) for check in checks
        ]
        errors = [max(number(row, "optimization_error"), 1e-16)] + [
            max(float(check["error"]), 1e-16) for check in checks
        ]

        figure, axis = plt.subplots(figsize=(7, 5), constrained_layout=True)
        color = "#4C78A8" if row["gate_set"] == "SNAP" else "#F58518"
        axis.semilogy(cutoffs, errors, "-o", color=color, markersize=4)
        axis.axhline(1e-2, color="#E45756", linestyle="--", label="stability limit (1e-2)")
        axis.axvline(
            int(row["cutoff_per_mode"]),
            color="#54A24B",
            linestyle=":",
            label="optimization cutoff",
        )
        axis.set_xlabel("Per-mode Fock truncation")
        axis.set_ylabel("Infidelity")
        axis.set_title(f"Replay stability: {row['example_id']}")
        axis.xaxis.set_major_locator(plt.MaxNLocator(integer=True))
        axis.grid(True, which="both", alpha=0.3)
        axis.legend(fontsize="small")

        path = plot_dir / f"{row['example_id']}.png"
        figure.savefig(path, dpi=180)
        figures_and_paths.append((figure, path))
    return figures_and_paths


def plot_minimum_depth(rows, output_dir):
    plot_dir = output_dir / "minimum_depth"
    figures_and_paths = []
    for row in rows:
        relative = row.get("depth_sweep_file", "")
        if row["target_kind"] != "unitary" or not relative:
            continue
        probes = read_rows(output_dir / relative)
        valid = [probe for probe in probes if probe["best_optimization_error"]]
        if not valid:
            continue
        plot_dir.mkdir(parents=True, exist_ok=True)
        depths = [int(probe["depth"]) for probe in valid]
        errors = [max(float(probe["best_optimization_error"]), 1e-16) for probe in valid]

        figure, axis = plt.subplots(figsize=(7, 5), constrained_layout=True)
        axis.semilogy(depths, errors, "-o", color="#4C78A8", markersize=4)
        for probe, depth, error in zip(valid, depths, errors):
            if probe["accepted"] == "true":
                axis.semilogy(depth, error, "o", color="#54A24B", markersize=8,
                              label=f"minimum accepted depth: {depth}")
            elif probe["status"] == "replay_rejected":
                axis.semilogy(depth, error, "x", color="#E45756", markersize=7,
                              label="trained but replay-rejected")
        axis.axhline(1e-3, color="#E45756", linestyle="--", label="training limit (1e-3)")
        axis.set_xlabel("Circuit depth k")
        axis.set_ylabel("Best optimization infidelity")
        axis.set_title(f"Minimum-depth probe: {row['example_id']}")
        tick_step = max(1, math.ceil((max(depths) - min(depths)) / 8))
        ticks = list(range(min(depths), max(depths) + 1, tick_step))
        if ticks[-1] != max(depths):
            ticks.append(max(depths))
        axis.set_xticks(ticks)
        axis.grid(True, which="both", alpha=0.3)
        handles, labels = axis.get_legend_handles_labels()
        unique = dict(zip(labels, handles))
        axis.legend(unique.values(), unique.keys(), fontsize="small")

        path = plot_dir / f"{row['example_id']}.png"
        figure.savefig(path, dpi=180)
        figures_and_paths.append((figure, path))
    return figures_and_paths


def main():
    args = parse_args()
    summary_path = args.output_dir / "summary.csv"
    if not summary_path.is_file():
        raise SystemExit(f"summary file not found: {summary_path}")

    all_rows = read_rows(summary_path)
    rows = [row for row in all_rows if row["status"] == "success"]
    if not rows:
        raise SystemExit(f"no successful examples found in {summary_path}")

    figures_and_paths = [
        plot_metrics(rows, args.output_dir),
        plot_parameters(rows, args.output_dir),
        plot_replay_curves(rows, args.output_dir),
    ]
    figures_and_paths.extend(plot_circuit_replays(rows, args.output_dir))
    figures_and_paths.extend(plot_minimum_depth(rows, args.output_dir))
    for _, path in figures_and_paths:
        print(f"Wrote {path}")

    if args.show:
        plt.show()
    else:
        for figure, _ in figures_and_paths:
            plt.close(figure)


if __name__ == "__main__":
    main()
