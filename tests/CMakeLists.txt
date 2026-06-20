# Catch2 ships its CTest integration as Catch.cmake under its install share dir.
include("${Catch2_DIR}/Catch.cmake")

add_executable(
  lob_tests
  test_types.cpp
  test_version.cpp
  test_bitmap.cpp
  test_hugepage.cpp
  test_latency_histogram.cpp
  test_arena.cpp
  test_spsc.cpp
  test_messages.cpp
  test_fix_parser.cpp
  test_order.cpp
  test_id_index.cpp
  test_level.cpp
  test_book.cpp
  test_engine.cpp
  test_engine_differential.cpp
  test_engine_invariants.cpp
  test_engine_stress.cpp
  test_engine_snapshot.cpp
  test_json_recorder.cpp
  test_shard_router.cpp
  test_shard_runtime.cpp
  test_shard_egress_runtime.cpp
  test_egress_merger.cpp
  test_publisher_sink.cpp
  test_affinity.cpp)
find_package(Threads REQUIRED)
target_link_libraries(
  lob_tests
  PRIVATE lob::core
          lob::compiler_flags
          lob::warnings
          lob::sanitizers
          lob::hardening
          Catch2::Catch2WithMain
          Threads::Threads)

catch_discover_tests(lob_tests)
