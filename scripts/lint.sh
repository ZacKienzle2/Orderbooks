#!/usr/bin/env bash
# Run clang-tidy across the codebase using the release preset's compile_commands.json.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "${repo_root}"

preset="${LOB_PRESET:-linux-clang-rel}"
build_dir="build/${preset}"

if [ ! -f "${build_dir}/compile_commands.json" ]; then
  echo "compile_commands.json missing for preset '${preset}'."
  echo "Run: cmake --preset ${preset}"
  exit 1
fi

mapfile -t files < <(git ls-files 'include/**/*.hpp' 'src/**/*.cpp' 'src/**/*.hpp' 'tests/**/*.cpp' 'bench/**/*.cpp')

if [ "${#files[@]}" -eq 0 ]; then
  echo "No C++ files to lint."
  exit 0
fi

clang-tidy -p "${build_dir}" --quiet "${files[@]}"
