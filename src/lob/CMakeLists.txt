execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE LOB_GIT_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT LOB_GIT_SHA)
  set(LOB_GIT_SHA "unknown")
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in"
               "${CMAKE_BINARY_DIR}/generated/src/lob/version.cpp" @ONLY)

add_library(lob_core STATIC "${CMAKE_BINARY_DIR}/generated/src/lob/version.cpp")
add_library(lob::core ALIAS lob_core)

# Every public header, found rather than listed. The hand-kept list had drifted to 19 of the 29
# headers, missing the engine, the book and the parser. CONFIGURE_DEPENDS re-runs the glob when a
# header is added or removed.
file(GLOB _lob_public_headers CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/include/lob/*.hpp")
target_sources(lob_core PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_SOURCE_DIR}/include" FILES
                               ${_lob_public_headers})

target_include_directories(lob_core PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(
  lob_core
  PUBLIC Boost::headers
  PRIVATE lob::compiler_flags lob::warnings lob::sanitizers lob::hardening)
