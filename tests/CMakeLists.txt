include(Catch)

add_executable(lob_tests
  test_types.cpp
  test_version.cpp
  test_bitmap.cpp
  test_arena.cpp
  test_spsc.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(
  lob_tests
  PRIVATE lob::core
          lob::compiler_flags
          lob::warnings
          lob::sanitizers
          lob::hardening
          Catch2::Catch2WithMain
          Threads::Threads
)

catch_discover_tests(lob_tests)
