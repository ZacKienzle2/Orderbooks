find_package(Threads REQUIRED)
add_executable(lob_gateway main.cpp)
target_link_libraries(
  lob_gateway PRIVATE lob::core lob::compiler_flags lob::warnings
                      lob::sanitizers lob::hardening Threads::Threads)
