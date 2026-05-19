include_guard(GLOBAL)

add_library(lob_hardening INTERFACE)
add_library(lob::hardening ALIAS lob_hardening)

option(LOB_ENABLE_HARDENING "Enable hardening flags in non-Debug builds" ON)

if(NOT LOB_ENABLE_HARDENING OR MSVC)
  return()
endif()

include(CheckCXXCompilerFlag)

set(_lob_hard_candidate_compile
    -fstack-protector-strong
    -fstack-clash-protection
    -fcf-protection=full
    -fPIE
)
set(_lob_hard_candidate_link
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -pie
)

set(_lob_hard_compile "")
foreach(_flag IN LISTS _lob_hard_candidate_compile)
  string(MAKE_C_IDENTIFIER "HAVE_${_flag}" _var)
  check_cxx_compiler_flag("${_flag}" ${_var})
  if(${_var})
    list(APPEND _lob_hard_compile "${_flag}")
  endif()
endforeach()

target_compile_options(lob_hardening INTERFACE
  ${_lob_hard_compile}
  $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>
)
target_link_options(lob_hardening INTERFACE ${_lob_hard_candidate_link})
