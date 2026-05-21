include_guard(GLOBAL)

find_package(Boost REQUIRED COMPONENTS headers)
find_package(fmt CONFIG REQUIRED)

if(LOB_BUILD_TESTS)
  find_package(Catch2 3 CONFIG REQUIRED)
  find_package(rapidcheck CONFIG REQUIRED)
endif()

if(LOB_BUILD_BENCH)
  find_package(benchmark CONFIG REQUIRED)
  find_package(nanobench CONFIG REQUIRED)
endif()
