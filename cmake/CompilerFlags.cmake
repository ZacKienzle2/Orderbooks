# cmake-lint: disable=C0103
include_guard(GLOBAL)
include(CheckCXXCompilerFlag)

add_library(lob_compiler_flags INTERFACE)
add_library(lob::compiler_flags ALIAS lob_compiler_flags)

target_compile_features(lob_compiler_flags INTERFACE cxx_std_20)

# Probe each candidate flag with -Werror so that compilers which accept the flag
# with a warning (e.g. Apple Clang on -fno-semantic-interposition) are correctly
# detected as "unsupported".
function(_lob_probe_flag flag out_var)
  set(_old "${CMAKE_REQUIRED_FLAGS}")
  set(CMAKE_REQUIRED_FLAGS "${flag} -Werror")
  string(MAKE_C_IDENTIFIER "LOB_HAVE_CXX_${flag}" _id)
  check_cxx_compiler_flag("${flag}" ${_id})
  set(CMAKE_REQUIRED_FLAGS "${_old}")
  set(${out_var}
      "${${_id}}"
      PARENT_SCOPE)
endfunction()

if(MSVC)
  target_compile_options(
    lob_compiler_flags INTERFACE /permissive- /Zc:__cplusplus /Zc:preprocessor
                                 /utf-8)
else()
  target_compile_options(
    lob_compiler_flags
    INTERFACE -fno-omit-frame-pointer -fdiagnostics-color=always
              -fvisibility=hidden -fvisibility-inlines-hidden)

  if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL
                                            "RelWithDebInfo")
    # -fno-trapping-math and -ffp-contract=fast both relax floating-point
    # semantics. The engine itself is integer-only today, but any future FP
    # analytics translation unit linked under these flags will have FMA
    # contraction permitted and trapping ops removed; rounding may differ from a
    # strict-IEEE build. Re-evaluate before adding any production FP risk path.
    set(_lob_perf_candidates
        -fno-plt
        -fno-semantic-interposition
        -fstrict-aliasing
        -falign-functions=64
        -falign-loops=32
        -fno-trapping-math
        -ffp-contract=fast)
    set(_lob_perf_compile "")
    foreach(_flag IN LISTS _lob_perf_candidates)
      _lob_probe_flag("${_flag}" _ok)
      if(_ok)
        list(APPEND _lob_perf_compile "${_flag}")
      endif()
    endforeach()
    target_compile_options(lob_compiler_flags INTERFACE ${_lob_perf_compile})
  endif()
endif()

if(LOB_ENABLE_NATIVE AND NOT MSVC)
  target_compile_options(lob_compiler_flags INTERFACE -march=native
                                                      -mtune=native)
elseif(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  target_compile_options(lob_compiler_flags INTERFACE -march=x86-64-v3)
endif()
