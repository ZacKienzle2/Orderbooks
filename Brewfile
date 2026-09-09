# macOS prerequisites for the build; `brew bundle` installs them. The tools the
# hooks run (clang-format, cmake-format, typos, gitleaks, actionlint,
# shellcheck) are installed by pre-commit into its own cache and are not
# listed. llvm is keg-only: its bin directory goes on PATH, which the dotfiles
# do, and clangd, clang-tidy and run-clang-tidy come from it.
brew "cmake"
brew "ninja"
brew "llvm"
brew "ccache"
# The cppcheck hook is at the manual stage and runs the binary from PATH.
brew "cppcheck"
brew "uv"
brew "pre-commit"
brew "just"
