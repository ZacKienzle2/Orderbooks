add_executable(lob_bench bench_smoke.cpp)
target_link_libraries(
  lob_bench
  PRIVATE lob::core
          lob::compiler_flags
          lob::warnings
          benchmark::benchmark_main
)
