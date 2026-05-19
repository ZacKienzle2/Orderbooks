cmake_minimum_required(VERSION 3.28)

# Toolchain pre-set via CMakePresets.json (vcpkg manifest mode).
project(
  Orderbooks
  VERSION 0.1.0
  DESCRIPTION "Low-latency limit order book and matching engine"
  HOMEPAGE_URL "https://github.com/ZacKienzle2/Orderbooks"
  LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(StandardProjectSettings)
include(CompilerFlags)
include(CompilerWarnings)
include(Sanitizers)
include(Hardening)
include(Dependencies)

option(LOB_BUILD_TESTS "Build unit and property tests" ON)
option(LOB_BUILD_BENCH "Build microbenchmarks" ON)
option(LOB_BUILD_FUZZ  "Build libFuzzer harnesses (clang only)" OFF)
option(LOB_ENABLE_LTO  "Enable Link-Time Optimisation in Release" OFF)
option(LOB_ENABLE_NATIVE "Enable -march=native (host-specific Release builds)" OFF)

if(LOB_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _lob_ipo_ok OUTPUT _lob_ipo_msg)
  if(NOT _lob_ipo_ok)
    message(FATAL_ERROR "LOB_ENABLE_LTO requested but unsupported: ${_lob_ipo_msg}")
  endif()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

add_subdirectory(src/lob)

if(LOB_BUILD_TESTS)
  enable_testing()
  include(CTest)
  add_subdirectory(tests)
endif()

if(LOB_BUILD_BENCH)
  add_subdirectory(bench)
endif()

include(CMakePackageConfigHelpers)
install(
  TARGETS lob_core
  EXPORT  lobTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/lob DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
  EXPORT lobTargets
  NAMESPACE lob::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lob
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/lobConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lobConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lob)
