include_guard(GLOBAL)

add_library(lob_compiler_flags INTERFACE)
add_library(lob::compiler_flags ALIAS lob_compiler_flags)

target_compile_features(lob_compiler_flags INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(lob_compiler_flags INTERFACE /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
else()
  target_compile_options(
    lob_compiler_flags
    INTERFACE
      -fno-omit-frame-pointer
      -fdiagnostics-color=always
      -fvisibility=hidden
      -fvisibility-inlines-hidden
  )

  if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_options(
      lob_compiler_flags
      INTERFACE
        -fno-plt
        -fno-semantic-interposition
        -fstrict-aliasing
    )
  endif()
endif()

if(LOB_ENABLE_NATIVE AND NOT MSVC)
  target_compile_options(lob_compiler_flags INTERFACE -march=native -mtune=native)
elseif(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  target_compile_options(lob_compiler_flags INTERFACE -march=x86-64-v3)
endif()
