include_guard(GLOBAL)

add_library(lob_sanitizers INTERFACE)
add_library(lob::sanitizers ALIAS lob_sanitizers)

set(LOB_SANITIZER "" CACHE STRING "Comma-separated list of -fsanitize values (address,undefined,thread,memory,fuzzer,leak)")

if(LOB_SANITIZER STREQUAL "")
  return()
endif()

if(MSVC)
  message(WARNING "LOB_SANITIZER ignored on MSVC")
  return()
endif()

string(REPLACE "," ";" _lob_san_list "${LOB_SANITIZER}")
set(_lob_san_flags "")
foreach(_san IN LISTS _lob_san_list)
  string(STRIP "${_san}" _san)
  list(APPEND _lob_san_flags "-fsanitize=${_san}")
endforeach()

target_compile_options(
  lob_sanitizers
  INTERFACE
    ${_lob_san_flags}
    -fno-omit-frame-pointer
    -fno-optimize-sibling-calls
)
target_link_options(lob_sanitizers INTERFACE ${_lob_san_flags})
