# Catch2 ships its CTest integration as Catch.cmake under its install share dir.
include("${Catch2_DIR}/Catch.cmake")

# The suite is every test_*.cpp beside this file. A hand-kept list drifts from the directory the
# moment a file is added under another name, and CONFIGURE_DEPENDS makes CMake re-glob when the
# directory changes rather than at configure time only.
file(GLOB lob_test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
list(SORT lob_test_sources)
add_executable(lob_tests ${lob_test_sources})
find_package(Threads REQUIRED)
# RapidCheck generates and shrinks the command sequences the model-based differential property runs;
# see tests/test_engine_model.cpp.
find_package(rapidcheck CONFIG REQUIRED)
# magic_enum enumerates the engine's policy and time-in-force enums, so the properties cover every
# enumerator without a list to keep in step.
find_package(magic_enum CONFIG REQUIRED)
target_link_libraries(
  lob_tests
  PRIVATE lob::core
          lob::compiler_flags
          lob::warnings
          lob::sanitizers
          lob::hardening
          Catch2::Catch2WithMain
          rapidcheck
          magic_enum::magic_enum
          Threads::Threads)

catch_discover_tests(lob_tests)
