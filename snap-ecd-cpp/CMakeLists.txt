cmake_minimum_required(VERSION 3.24)
project(snap_ecd_cpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for IDE/clangd include resolution

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Homebrew ships Eigen 5.x; the 3.4+ API we use is unchanged, so no version pin.
find_package(Eigen3 REQUIRED)

# ── Third-party header-only deps ─────────────────────────────────────────────
include(FetchContent)

# LBFGSpp: header-only L-BFGS built on Eigen (scipy.optimize L-BFGS-B analogue).
# Point SOURCE_SUBDIR at include/ (which has no CMakeLists.txt) so FetchContent
# downloads the headers but does NOT configure LBFGSpp's own CMake project —
# its cmake_minimum_required predates CMake 3.5 and no longer configures.
FetchContent_Declare(
  lbfgspp
  GIT_REPOSITORY https://github.com/yixuan/LBFGSpp.git
  GIT_TAG        v0.3.0
  SOURCE_SUBDIR  include
)
FetchContent_MakeAvailable(lbfgspp)

# ── Core library ─────────────────────────────────────────────────────────────
# One consolidated finder per decomposition, over the shared header-only core/.
add_library(snap_ecd
  src/decomposition.cpp
  src/snap_parameter_finder.cpp
  src/ecd_parameter_finder.cpp
)
target_include_directories(snap_ecd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# LBFGSpp headers live under ${lbfgspp_SOURCE_DIR}/include (target name varies
# across versions, so add the include dir directly).
target_include_directories(snap_ecd PRIVATE ${lbfgspp_SOURCE_DIR}/include)
target_link_libraries(snap_ecd PUBLIC Eigen3::Eigen)

# ── Tutorial example runner ─────────────────────────────────────────────────
# This is a consumer of the public API, not part of the library interface.
option(SNAP_ECD_BUILD_EXAMPLES
       "Build the tutorial and data-generation runner" ON)
if(SNAP_ECD_BUILD_EXAMPLES)
  add_executable(snap_ecd_examples examples/example_runner.cpp)
  target_link_libraries(snap_ecd_examples PRIVATE snap_ecd)

  add_executable(snap_ecd_haar_benchmark examples/haar_benchmark.cpp)
  target_link_libraries(snap_ecd_haar_benchmark PRIVATE snap_ecd)
endif()

# Optional libtorch reverse-mode gradient backend (GradMethod::Autodiff). Off by
# default so the core build stays dependency-light. Enable with:
#   cmake -S . -B build -DECD_WITH_TORCH=ON \
#     -DCMAKE_PREFIX_PATH="$(python -c 'import torch;print(torch.utils.cmake_prefix_path)')"
option(ECD_WITH_TORCH "Build the libtorch autodiff ECD gradient backend" OFF)
if(ECD_WITH_TORCH)
  find_package(Torch REQUIRED)
  target_link_libraries(snap_ecd PUBLIC ${TORCH_LIBRARIES})
  target_compile_definitions(snap_ecd PUBLIC ECD_WITH_TORCH)
  target_compile_options(snap_ecd PUBLIC ${TORCH_CXX_FLAGS})
endif()

# ── Tests (Catch2, fetched at configure time) ────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

enable_testing()
add_executable(snap_ecd_tests
  tests/test_core.cpp
  tests/test_decomposition.cpp
  tests/test_fourier_stability.cpp
  tests/snap/test_gates.cpp
  tests/snap/test_circuit.cpp
  tests/snap/test_cost.cpp
  tests/snap/test_gradient.cpp
  tests/snap/test_optimizer.cpp
  tests/snap/test_end_to_end.cpp
  tests/ecd/test_gates.cpp
  tests/ecd/test_circuit.cpp
  tests/ecd/test_optimizer.cpp
  tests/ecd/test_end_to_end.cpp
  tests/ecd/test_multimode_end_to_end.cpp
)
if(ECD_WITH_TORCH)
  target_sources(snap_ecd_tests PRIVATE
    tests/ecd/test_torch_gradient.cpp
    tests/ecd/test_torch_end_to_end.cpp
  )
endif()
target_link_libraries(snap_ecd_tests PRIVATE snap_ecd Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(snap_ecd_tests)
