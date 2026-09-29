add_executable(lob_profile main.cpp)
target_link_libraries(lob_profile PRIVATE lob::core lob::compiler_flags lob::warnings
                                          lob::sanitizers lob::hardening)

# Under LOB_PGO=generate the profiler's workloads are the training set. ctest asks the built
# profiler for them with --list when it reads this directory's tests and runs each one, so a
# workload added to the profiler is trained on without a second list kept here.
if(LOB_PGO STREQUAL "generate")
  file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/workloads.cmake"
    CONTENT
      "execute_process(COMMAND [[$<TARGET_FILE:lob_profile>]] --list OUTPUT_VARIABLE listing)
string(REGEX MATCHALL \"[^\\n]+\" lines \"\${listing}\")
foreach(line IN LISTS lines)
  string(REGEX MATCH \"^[^ ]+\" workload \"\${line}\")
  add_test(lob_profile_\${workload} [[$<TARGET_FILE:lob_profile>]] --workload \${workload})
  set_tests_properties(lob_profile_\${workload} PROPERTIES ${LOB_PROFILE_TEST_PROPERTIES})
endforeach()
")
  set_property(
    DIRECTORY
    APPEND
    PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/workloads.cmake")
endif()
