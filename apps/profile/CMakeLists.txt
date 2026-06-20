add_executable(lob_profile main.cpp)
target_link_libraries(
  lob_profile PRIVATE lob::core lob::compiler_flags lob::warnings
                      lob::sanitizers lob::hardening)
