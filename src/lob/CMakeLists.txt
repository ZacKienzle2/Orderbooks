execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE LOB_GIT_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT LOB_GIT_SHA)
  set(LOB_GIT_SHA "unknown")
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in"
  "${CMAKE_BINARY_DIR}/generated/src/lob/version.cpp"
  @ONLY
)

add_library(lob_core STATIC "${CMAKE_BINARY_DIR}/generated/src/lob/version.cpp")
add_library(lob::core ALIAS lob_core)

target_sources(
  lob_core
  PUBLIC FILE_SET HEADERS
         BASE_DIRS "${CMAKE_SOURCE_DIR}/include"
         FILES "${CMAKE_SOURCE_DIR}/include/lob/types.hpp"
               "${CMAKE_SOURCE_DIR}/include/lob/version.hpp"
               "${CMAKE_SOURCE_DIR}/include/lob/bitmap.hpp"
               "${CMAKE_SOURCE_DIR}/include/lob/arena.hpp"
               "${CMAKE_SOURCE_DIR}/include/lob/spsc_ring.hpp"
               "${CMAKE_SOURCE_DIR}/include/lob/json_recorder.hpp"
)

target_include_directories(
  lob_core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  lob_core
  PUBLIC Boost::headers
         unordered_dense::unordered_dense
  PRIVATE lob::compiler_flags
          lob::warnings
          lob::sanitizers
          lob::hardening
          fmt::fmt
)
