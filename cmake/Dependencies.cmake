include_guard(GLOBAL)

find_package(Boost REQUIRED COMPONENTS headers)

if(LOB_BUILD_TESTS)
  find_package(Catch2 3 CONFIG REQUIRED)
endif()

if(LOB_BUILD_BENCH)
  find_package(benchmark CONFIG REQUIRED)
endif()
