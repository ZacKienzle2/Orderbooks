include_guard(GLOBAL)

add_library(lob_hardening INTERFACE)
add_library(lob::hardening ALIAS lob_hardening)

option(LOB_ENABLE_HARDENING "Enable hardening flags in non-Debug builds" ON)

if(NOT LOB_ENABLE_HARDENING OR MSVC)
  return()
endif()

include(CheckCompilerFlag)
include(CheckLinkerFlag)

# check_compiler_flag and check_linker_flag fail a flag the toolchain answers with any diagnostic,
# so one that is accepted with a warning (Apple Clang on -fcf-protection or
# -fstack-clash-protection) counts as unsupported. Position independence comes from
# CMAKE_POSITION_INDEPENDENT_CODE and check_pie_supported, not from a flag here.
set(_lob_hard_candidate_compile -fstack-protector-strong -fstack-clash-protection
                                -fcf-protection=full)
set(_lob_hard_candidate_link -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)

set(_lob_hard_compile "")
foreach(_flag IN LISTS _lob_hard_candidate_compile)
  string(MAKE_C_IDENTIFIER "LOB_HAVE_CXX_${_flag}" _var)
  check_compiler_flag(CXX "${_flag}" ${_var})
  if(${_var})
    list(APPEND _lob_hard_compile "${_flag}")
  endif()
endforeach()

set(_lob_hard_link "")
foreach(_flag IN LISTS _lob_hard_candidate_link)
  string(MAKE_C_IDENTIFIER "LOB_HAVE_LD_${_flag}" _var)
  check_linker_flag(CXX "${_flag}" ${_var})
  if(${_var})
    list(APPEND _lob_hard_link "${_flag}")
  endif()
endforeach()

target_compile_options(lob_hardening INTERFACE ${_lob_hard_compile}
                                               $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
target_link_options(lob_hardening INTERFACE ${_lob_hard_link})
