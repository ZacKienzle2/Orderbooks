#!/usr/bin/env bash
# Format all C++ + CMake sources in-place.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "${repo_root}"

mapfile -t cpp_files < <(git ls-files '*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hxx' '*.h' '*.cu' '*.cuh')
mapfile -t cmake_files < <(git ls-files 'CMakeLists.txt' '**/CMakeLists.txt' '*.cmake')

if [ "${#cpp_files[@]}" -gt 0 ]; then
  clang-format -i --style=file "${cpp_files[@]}"
fi

if [ "${#cmake_files[@]}" -gt 0 ]; then
  cmake-format -i "${cmake_files[@]}"
fi

echo "Formatted ${#cpp_files[@]} C++ and ${#cmake_files[@]} CMake files."
