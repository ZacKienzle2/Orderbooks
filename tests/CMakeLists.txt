include(Catch)

add_executable(lob_tests test_types.cpp test_version.cpp)
target_link_libraries(
  lob_tests
  PRIVATE lob::core
          lob::compiler_flags
          lob::warnings
          lob::sanitizers
          lob::hardening
          Catch2::Catch2WithMain
)

catch_discover_tests(lob_tests)
