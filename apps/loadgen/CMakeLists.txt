find_package(Threads REQUIRED)
add_executable(lob_loadgen main.cpp)
target_link_libraries(
  lob_loadgen PRIVATE lob::core lob::compiler_flags lob::warnings
                      lob::sanitizers lob::hardening Threads::Threads)
