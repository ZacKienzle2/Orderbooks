include_guard(GLOBAL)

add_library(lob_sanitizers INTERFACE)
add_library(lob::sanitizers ALIAS lob_sanitizers)

set(LOB_SANITIZER
    ""
    CACHE STRING
          "Comma-separated list of -fsanitize values (address,undefined,thread,memory,fuzzer,leak)")

if(LOB_SANITIZER STREQUAL "")
  return()
endif()

if(MSVC)
  message(WARNING "LOB_SANITIZER ignored on MSVC")
  return()
endif()

# clang and gcc both take the comma-separated list -fsanitize= is documented with. The frame pointer
# the sanitizer reports need comes from lob::compiler_flags, which every target links.
target_compile_options(lob_sanitizers INTERFACE "-fsanitize=${LOB_SANITIZER}"
                                                -fno-optimize-sibling-calls)
target_link_options(lob_sanitizers INTERFACE "-fsanitize=${LOB_SANITIZER}")
