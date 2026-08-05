# Running the example scripts

## 1. Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build --parallel
```

## 2. Run the tutorial example runner

```bash
./build/snap_ecd_examples --output-dir example_output
```

Optional flags:

- `--seed N` — offset the built-in example seeds.
- `--depth-analysis` — also probe the minimum stable depth per unitary (slower).
- `--help` — list all options.

## 3. Plot the tutorial results

```bash
python3 -m pip install -r examples/requirements.txt
python3 examples/plot_results.py example_output
```

Add `--show` to open the figures interactively.

## 4. Run the Haar-state benchmark

```bash
./build/snap_ecd_haar_benchmark --output-dir haar_output
```

## 5. Plot the Haar benchmark results

```bash
python3 examples/plot_haar_results.py haar_output \
  --metriq-baseline /path/to/d4_k8_nu25_seed42.npz
```

The `--metriq-baseline` argument is optional; omit it to plot the C++ results alone.
