# snap-ecd-cpp

C++ implementation for two bosonic gate-decomposition schemes:

- **SNAP + Displacement** — `snap::pulse_parameter_finder_*`
- **ECD + Rotation** — `ecd::ECDParameterFinder`

Both finders are capable of unitary and state preparation (default is unitary prep).
Both decomposition straregies share the same front end, `decomp::decompose()`,
which is implemented in `src/decomposition.cpp`. 

The original python implementations live under `reference/python/`.

## Build

Requires CMake ≥ 3.24, a C++20 compiler, and Eigen 3.4+ (`brew install eigen`).
LBFGSpp and Catch2 are fetched automatically at configure time.

```bash
cmake -S . -B build
cmake --build build --parallel
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Public API

- `include/decomposition.hpp` — `decomp::decompose(target, modes, gate_set, options)`,
  the main entry point.
- `include/snap_parameter_finder.hpp` — SNAP gate decomposition (can only be used for single mode).
- `include/ecd_parameter_finder.hpp` — ECD gates decomposition.

## Examples

See `examples/README.md` for the tutoial on running `example_runner.cpp` and `haar_benchmark.cpp`.

