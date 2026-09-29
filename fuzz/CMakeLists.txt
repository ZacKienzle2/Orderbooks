# libFuzzer harnesses for the code that reads bytes this process did not write: the FIX parser,
# snapshot restore, and the gateway's wire decode.
#
# The sanitizers come from the preset (LOB_SANITIZER, which linux-clang-fuzz sets to
# address,undefined) through lob::sanitizers, so that set is declared in one place rather than
# repeated here. -fsanitize=fuzzer belongs to these targets alone, because it supplies main() and
# every other executable in the project has its own. libFuzzer ships with clang, so the targets
# build only under it.
#
# Each harness keeps its corpus in LOB_FUZZ_CORPUS_DIR/<harness>, where a fuzzing run adds the
# inputs that reached new coverage. Each is also a test that replays that corpus once (-runs=0),
# which is the check an engine change owes every corpus, and which the fuzz-coverage preset runs to
# measure what the corpora reach.
#
# With LOB_FUZZ_SECONDS above zero, each harness also fuzzes its corpus for that long as a test, so
# ctest runs the harnesses side by side and a budget costs its length once rather than once per
# harness. A run starts after its own corpus replay and writes any crash-<sha1> beside the corpora,
# and replaying one is the same binary with that file as its only argument.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "LOB_BUILD_FUZZ: skipped, libFuzzer needs clang")
    return()
endif()

set(LOB_FUZZ_CORPUS_DIR
    "${PROJECT_SOURCE_DIR}/artifacts/fuzz/corpus"
    CACHE PATH
    "Directory holding one corpus directory per fuzz harness"
)
set(LOB_FUZZ_SECONDS
    0
    CACHE STRING
    "Seconds each harness fuzzes as a test, or 0 for no fuzzing tests"
)
cmake_path(GET LOB_FUZZ_CORPUS_DIR PARENT_PATH _lob_fuzz_artifacts)

# The harnesses are every fuzz_*.cpp beside this file, as the test suite is every test_*.cpp.
file(
    GLOB _lob_fuzz_sources
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/fuzz_*.cpp"
)
foreach(source IN LISTS _lob_fuzz_sources)
    cmake_path(GET source STEM stem)
    string(REGEX REPLACE "^fuzz_" "" target "${stem}")
    add_executable(lob_fuzz_${target} "${source}")
    target_include_directories(
        lob_fuzz_${target}
        PRIVATE "${PROJECT_SOURCE_DIR}"
    )
    target_link_libraries(
        lob_fuzz_${target}
        PRIVATE
            lob::core
            lob::compiler_flags
            lob::warnings
            lob::sanitizers
            lob::hardening
    )
    # A harness states the invariants a sanitizer cannot see, so its assertions are live whatever the
    # build type would have done to NDEBUG.
    target_compile_options(
        lob_fuzz_${target}
        PRIVATE -fsanitize=fuzzer -UNDEBUG
    )
    target_link_options(lob_fuzz_${target} PRIVATE -fsanitize=fuzzer)

    set(corpus "${LOB_FUZZ_CORPUS_DIR}/${target}")
    file(MAKE_DIRECTORY "${corpus}")
    add_test(
        NAME lob_fuzz_${target}_replay
        COMMAND lob_fuzz_${target} -runs=0 "${corpus}"
    )
    if(LOB_PROFILE_DIR)
        set_tests_properties(
            lob_fuzz_${target}_replay
            PROPERTIES ${LOB_PROFILE_TEST_PROPERTIES}
        )
    endif()
    if(LOB_FUZZ_SECONDS GREATER 0)
        add_test(
            NAME lob_fuzz_${target}_run
            COMMAND
                lob_fuzz_${target} -max_total_time=${LOB_FUZZ_SECONDS}
                -print_final_stats=1 "${corpus}"
        )
        set_tests_properties(
            lob_fuzz_${target}_run
            PROPERTIES
                DEPENDS lob_fuzz_${target}_replay
                WORKING_DIRECTORY "${_lob_fuzz_artifacts}"
        )
    endif()
endforeach()
