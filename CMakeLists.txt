cmake_minimum_required(VERSION 3.30)

# Toolchain pre-set via CMakePresets.json (vcpkg manifest mode).
project(
  Orderbooks
  VERSION 0.1.0
  DESCRIPTION "Low-latency limit order book and matching engine"
  HOMEPAGE_URL "https://github.com/ZacKienzle2/Orderbooks"
  LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Build-shape switches must be declared before Dependencies.cmake runs so the
# conditional find_package calls (Catch2, RapidCheck under LOB_BUILD_TESTS;
# Google Benchmark + nanobench under LOB_BUILD_BENCH) see the correct values on
# the first configure of a clean build directory.
option(LOB_BUILD_TESTS "Build unit and property tests" ON)
option(LOB_BUILD_BENCH "Build microbenchmarks" ON)
option(LOB_BUILD_FUZZ "Build libFuzzer harnesses (clang only)" OFF)
option(LOB_ENABLE_LTO "Enable Link-Time Optimisation in Release" OFF)
option(LOB_ENABLE_NATIVE "Enable -march=native (host-specific Release builds)"
       OFF)

include(StandardProjectSettings)
include(CompilerFlags)
include(CompilerWarnings)
include(Sanitizers)
include(Hardening)
include(Dependencies)

if(LOB_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _lob_ipo_ok OUTPUT _lob_ipo_msg)
  if(NOT _lob_ipo_ok)
    message(
      FATAL_ERROR "LOB_ENABLE_LTO requested but unsupported: ${_lob_ipo_msg}")
  endif()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

add_subdirectory(src/lob)
add_subdirectory(apps/replay)
add_subdirectory(apps/loadgen)

if(LOB_BUILD_TESTS)
  enable_testing()
  include(CTest)
  add_subdirectory(tests)
endif()

if(LOB_BUILD_BENCH)
  add_subdirectory(bench)
endif()

# Install rules deferred until the engine ABI is settled; consumers should
# currently embed the project via add_subdirectory or FetchContent.
