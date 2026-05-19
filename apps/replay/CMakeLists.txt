add_executable(lob_replay main.cpp)
target_link_libraries(
  lob_replay
  PRIVATE lob::core
          lob::compiler_flags
          lob::warnings
          lob::sanitizers
          lob::hardening
)
