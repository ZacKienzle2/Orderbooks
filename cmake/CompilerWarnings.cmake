include_guard(GLOBAL)

add_library(lob_warnings INTERFACE)
add_library(lob::warnings ALIAS lob_warnings)

set(_lob_warnings_gnu
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wcast-qual
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wmisleading-indentation
    -Wfloat-equal
    -Wundef
    -Wzero-as-null-pointer-constant
    -Wno-unknown-pragmas
)

set(_lob_warnings_gcc
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -Wuseless-cast
    -Wsuggest-override
)

set(_lob_warnings_clang
    -Wshadow-all
    -Wextra-semi
    -Wnewline-eof
    -Wdocumentation
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
)

set(_lob_warnings_msvc
    /W4
    /permissive-
    /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296
    /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14619
    /w14640 /w14826 /w14905 /w14906 /w14928
)

option(LOB_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(lob_warnings INTERFACE ${_lob_warnings_gnu} ${_lob_warnings_gcc})
  if(LOB_WARNINGS_AS_ERRORS)
    target_compile_options(lob_warnings INTERFACE -Werror)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(lob_warnings INTERFACE ${_lob_warnings_gnu} ${_lob_warnings_clang})
  if(LOB_WARNINGS_AS_ERRORS)
    target_compile_options(lob_warnings INTERFACE -Werror)
  endif()
elseif(MSVC)
  target_compile_options(lob_warnings INTERFACE ${_lob_warnings_msvc})
  if(LOB_WARNINGS_AS_ERRORS)
    target_compile_options(lob_warnings INTERFACE /WX)
  endif()
endif()
