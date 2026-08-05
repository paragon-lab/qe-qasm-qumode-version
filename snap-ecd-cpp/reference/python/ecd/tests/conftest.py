import sys
import os

# Force JAX to use the CPU backend for tests.
# The Apple Metal GPU backend (default on M-series Macs) does not support
# complex128, which causes failures in all JAX-based tests.  CPU supports
# complex128 and is sufficient for the small problem sizes used here.
os.environ["JAX_PLATFORMS"] = "cpu"

# Headless matplotlib for plotting tests.
os.environ["MPLBACKEND"] = "Agg"

# Add project root to path so tests can import source modules directly.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
