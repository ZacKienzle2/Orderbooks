cmake_minimum_required(VERSION 3.30)

# Toolchain pre-set via CMakePresets.json (vcpkg manifest mode).
project(
  Orderbooks
  VERSION 0.1.0
  DESCRIPTION "Low-latency limit order book and matching engine"
  HOMEPAGE_URL "https://github.com/ZacKienzle2/Orderbooks"
  LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Build-shape switches must be declared before Dependencies.cmake runs so the conditional
# find_package calls (Catch2 under LOB_BUILD_TESTS; Google Benchmark under LOB_BUILD_BENCH) see the
# correct values on the first configure of a clean build directory.
option(LOB_BUILD_TESTS "Build unit and property tests" ON)
option(LOB_BUILD_BENCH "Build microbenchmarks" ON)
option(LOB_BUILD_FUZZ "Build libFuzzer harnesses (clang only)" OFF)
option(LOB_ENABLE_LTO "Enable Link-Time Optimisation in Release" OFF)
set(LOB_PGO
    "off"
    CACHE STRING "Profile-guided optimisation: off, generate, or use")
set(LOB_PGO_PROFILE
    "${CMAKE_SOURCE_DIR}/artifacts/pgo/train.profdata"
    CACHE FILEPATH "Merged profile read when LOB_PGO=use")
option(LOB_ENABLE_NATIVE "Enable -march=native (host-specific Release builds)" OFF)
option(LOB_COVERAGE "Instrument for llvm-cov source-based coverage" OFF)
option(LOB_HEADER_SELF_CONTAINMENT
       "Compile each public header on its own, so analysers see every one" ON)

include(StandardProjectSettings)
include(CompilerFlags)
include(CompilerWarnings)
include(Sanitizers)
include(Hardening)
include(Dependencies)
include(HeaderSelfContainment)

if(LOB_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _lob_ipo_ok OUTPUT _lob_ipo_msg)
  if(NOT _lob_ipo_ok)
    message(FATAL_ERROR "LOB_ENABLE_LTO requested but unsupported: ${_lob_ipo_msg}")
  endif()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Testing is on whatever the build shape, because the profiler's training runs and the fuzz corpus
# replays are tests too, and a preset that builds no unit tests still runs those.
include(CTest)

add_subdirectory(src/lob)

if(LOB_HEADER_SELF_CONTAINMENT)
  lob_add_header_self_containment_target(lob_header_tus)
endif()
add_subdirectory(apps/replay)
add_subdirectory(apps/loadgen)
add_subdirectory(apps/gateway)
add_subdirectory(apps/profile)

if(LOB_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(LOB_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(LOB_BUILD_FUZZ)
  add_subdirectory(fuzz)
endif()

# An instrumented build pools every process's raw profile under LOB_PROFILE_DIR, one file per
# binary, which clang's %m pattern merges as each process exits. The tests that write profiles are
# setup tests of the lob_profiles fixture, so the merge below runs once all of them have, and the
# pool is emptied first so a rebuilt binary's profile is never merged with its predecessor's.
if(LOB_PROFILE_DIR)
  add_test(NAME lob_profiles_clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${LOB_PROFILE_DIR}")
  set_tests_properties(lob_profiles_clean PROPERTIES FIXTURES_SETUP lob_profiles_clean)
  add_test(NAME lob_profiles_merge COMMAND "${LLVM_PROFDATA}" merge "-output=${LOB_PROFDATA}"
                                           "${LOB_PROFILE_DIR}")
  set_tests_properties(lob_profiles_merge PROPERTIES FIXTURES_REQUIRED lob_profiles)
endif()

# llvm-cov's arguments for the merged profile, written as a response file so the report names every
# instrumented binary the build defines and only this repository's sources, with nothing listed by
# hand: `llvm-cov report @build/<preset>/coverage.rsp`.
if(LOB_COVERAGE)
  set(_lob_cov_targets "")
  if(TARGET lob_tests)
    list(APPEND _lob_cov_targets lob_tests)
  endif()
  if(LOB_BUILD_FUZZ)
    get_property(
      _lob_fuzz_targets
      DIRECTORY fuzz
      PROPERTY BUILDSYSTEM_TARGETS)
    list(APPEND _lob_cov_targets ${_lob_fuzz_targets})
  endif()
  # The first binary is positional, as llvm-cov reads the positional arguments after it as sources.
  list(TRANSFORM _lob_cov_targets REPLACE "(.+)" "-object=$<TARGET_FILE:\\1>")
  list(
    TRANSFORM _lob_cov_targets
    REPLACE "^-object=" ""
    AT 0)
  list(JOIN _lob_cov_targets "\n" _lob_cov_objects)
  file(
    GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/coverage.rsp"
    CONTENT
      "-instr-profile=${LOB_PROFDATA}\n${_lob_cov_objects}\n${PROJECT_SOURCE_DIR}/include\n${PROJECT_SOURCE_DIR}/src\n${PROJECT_SOURCE_DIR}/apps\n"
  )
endif()

# Install rules deferred until the engine ABI is settled; consumers should currently embed the
# project via add_subdirectory or FetchContent.
