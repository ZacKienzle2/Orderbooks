# libFuzzer harnesses for the code that reads bytes this process did not write: the FIX parser,
# snapshot restore, and the gateway's wire decode.
#
# The sanitizers come from the preset (LOB_SANITIZER, which linux-clang-fuzz sets to
# address,undefined) through lob::sanitizers, so that set is declared in one place rather than
# repeated here. -fsanitize=fuzzer belongs to these targets alone, because it supplies main() and
# every other executable in the project has its own. libFuzzer ships with clang, so the targets
# build only under it.
#
# Run one to fuzz it, keeping its corpus between runs:
# build/linux-clang-fuzz/fuzz/lob_fuzz_fix_framed corpus/fix_framed \ -max_total_time=60 A crash is
# written to the working directory as crash-<sha1>; replaying it is the same binary with that file
# as its only argument.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(STATUS "LOB_BUILD_FUZZ: skipped, libFuzzer needs clang")
  return()
endif()

foreach(target fix_raw fix_framed snapshot_restore gateway_wire)
  add_executable(lob_fuzz_${target} fuzz_${target}.cpp)
  target_include_directories(lob_fuzz_${target} PRIVATE "${PROJECT_SOURCE_DIR}")
  target_link_libraries(lob_fuzz_${target} PRIVATE lob::core lob::compiler_flags lob::warnings
                                                   lob::sanitizers lob::hardening)
  # A harness states the invariants a sanitizer cannot see, so its assertions are live whatever the
  # build type would have done to NDEBUG.
  target_compile_options(lob_fuzz_${target} PRIVATE -fsanitize=fuzzer -UNDEBUG)
  target_link_options(lob_fuzz_${target} PRIVATE -fsanitize=fuzzer)
endforeach()
