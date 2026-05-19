#!/usr/bin/env bash
# Install the binaries required by the pre-commit local hooks and by the
# build (Ninja, vcpkg deps, clang-format / clang-tidy / cmake-format).
# Idempotent.
set -euo pipefail

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew not found. Install from https://brew.sh and rerun."
  exit 1
fi

# Build + format + lint tooling.
packages=(
  cmake
  ninja
  llvm
  cmake-format
  shellcheck
  typos-cli
  gitleaks
  actionlint
  pre-commit
  uv
  ccache
)

echo "==> brew install (idempotent) ..."
brew install "${packages[@]}" 2>&1 \
  | grep -vE '^Warning: .* is already installed' \
  || true

echo "==> Ensuring clang-format and clang-tidy on PATH from llvm keg ..."
if [ ! -L "$(brew --prefix)/bin/clang-format" ]; then
  brew link --overwrite --force llvm \
    || echo "    (link skipped; configure your PATH to include $(brew --prefix llvm)/bin manually)"
fi

if ! command -v uv >/dev/null 2>&1; then
  echo "uv missing after install. Aborting."
  exit 1
fi

echo "==> uv sync (tooling harness, dev group) ..."
uv sync --frozen

echo "==> pre-commit install --install-hooks ..."
uv run pre-commit install --install-hooks

echo
echo "Bootstrap complete. Next:"
echo "  export VCPKG_ROOT=\"\$HOME/code/vcpkg\""
echo "  cmake --preset macos-clang-dev"
echo "  cmake --build --preset macos-clang-dev --parallel"
echo "  ctest --preset macos-clang-dev --output-on-failure"
