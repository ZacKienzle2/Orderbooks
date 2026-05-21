add_executable(lob_bench bench_smoke.cpp bench_bitmap.cpp bench_arena.cpp
                         bench_spsc.cpp bench_engine.cpp)
find_package(Threads REQUIRED)
target_link_libraries(
  lob_bench PRIVATE lob::core lob::compiler_flags lob::warnings
                    benchmark::benchmark_main Threads::Threads)
