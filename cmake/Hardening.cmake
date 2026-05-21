include_guard(GLOBAL)

add_library(lob_hardening INTERFACE)
add_library(lob::hardening ALIAS lob_hardening)

option(LOB_ENABLE_HARDENING "Enable hardening flags in non-Debug builds" ON)

if(NOT LOB_ENABLE_HARDENING OR MSVC)
  return()
endif()

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag OPTIONAL
        RESULT_VARIABLE _lob_check_linker_flag_available)

# Probe each candidate flag with -Werror so that compilers which accept the flag
# with a warning (e.g. Apple Clang on -fcf-protection or
# -fstack-clash-protection) are correctly detected as "unsupported".
function(_lob_probe_compile flag out_var)
  set(_old "${CMAKE_REQUIRED_FLAGS}")
  set(CMAKE_REQUIRED_FLAGS "${flag} -Werror")
  string(MAKE_C_IDENTIFIER "LOB_HAVE_CXX_${flag}" _id)
  check_cxx_compiler_flag("${flag}" ${_id})
  set(CMAKE_REQUIRED_FLAGS "${_old}")
  set(${out_var}
      "${${_id}}"
      PARENT_SCOPE)
endfunction()

set(_lob_hard_candidate_compile
    -fstack-protector-strong -fstack-clash-protection -fcf-protection=full
    -fPIE)
set(_lob_hard_candidate_link -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
if(NOT APPLE)
  # ld64 ignores -pie at link time and emits an "argument unused" warning under
  # -Werror; the resulting binary is already PIE-by-default on macOS.
  list(APPEND _lob_hard_candidate_link -pie)
endif()

set(_lob_hard_compile "")
foreach(_flag IN LISTS _lob_hard_candidate_compile)
  _lob_probe_compile("${_flag}" _ok)
  if(_ok)
    list(APPEND _lob_hard_compile "${_flag}")
  endif()
endforeach()

set(_lob_hard_link "")
foreach(_flag IN LISTS _lob_hard_candidate_link)
  string(MAKE_C_IDENTIFIER "LOB_HAVE_LD_${_flag}" _var)
  if(_lob_check_linker_flag_available)
    check_linker_flag(CXX "${_flag}" ${_var})
  else()
    check_cxx_compiler_flag("${_flag}" ${_var})
  endif()
  if(${_var})
    list(APPEND _lob_hard_link "${_flag}")
  endif()
endforeach()

target_compile_options(
  lob_hardening INTERFACE ${_lob_hard_compile}
                          $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
target_link_options(lob_hardening INTERFACE ${_lob_hard_link})
