#!/usr/bin/env bash
#
# Engine profiling orchestrator.
#
# Builds the synthetic-data profiler (apps/profile, lob_profile) and runs it
# under a set of analysis plugins that surface micro-optimisations,
# implementation errors, and missed opportunities. The profiler drives one
# engine on one thread with a deterministic workload, so the counters belong to
# the engine rather than to thread scheduling or a random op-dispatch.
#
# Plugins:
#   perf       per-workload perf stat counters (cyc/op, IPC, branch and cache
#              miss rates), the micro-optimisation signal.
#   sanitize   an ASAN and UBSAN soak over the deep mix, the implementation-error
#              signal.
#   record     perf record source-line hot spots over the deep mix, the
#              missed-opportunity signal.
#
# Usage:
#   scripts/profile.sh [--plugin perf|sanitize|record|all] [--ops N] [--depth D]
#
# Needs a Linux host with perf for the perf and record plugins. The cyc/op unit
# is reference cycles; divide by the host frequency for nanoseconds.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plugin="all"
ops="20000000"
depth="40000"
workloads=(deep submit cancel modifyp modifyq cross sweep)

while [[ $# -gt 0 ]]; do
  case "$1" in
    --plugin) plugin="$2"; shift 2 ;;
    --ops) ops="$2"; shift 2 ;;
    --depth) depth="$2"; shift 2 ;;
    -h | --help)
      sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

# Locate the vcpkg include directory that holds boost-intrusive, trying the
# in-tree build trees first, then a developer vcpkg, then the system.
find_include() {
  local cand
  for cand in \
    "${root}"/build/*/vcpkg_installed/x64-linux/include \
    "${VCPKG_ROOT:-}/installed/x64-linux/include" \
    "${HOME}/vcpkg/installed/x64-linux/include"; do
    if [[ -f "${cand}/boost/intrusive/list.hpp" ]]; then
      echo "${cand}"
      return 0
    fi
  done
  if [[ -f /usr/include/boost/intrusive/list.hpp ]]; then
    echo "/usr/include"
    return 0
  fi
  echo "profile.sh: no boost-intrusive include found; build vcpkg deps first" >&2
  return 1
}

cxx="${CXX:-clang++}"
inc="$(find_include)"
src="${root}/apps/profile/main.cpp"
common=(-std=c++20 -march=native -I "${root}/include" -isystem "${inc}")
bin_rel="/tmp/lob_profile_rel"
bin_san="/tmp/lob_profile_san"

build_rel() {
  [[ -x "${bin_rel}" && "${bin_rel}" -nt "${src}" ]] && return 0
  "${cxx}" "${common[@]}" -O3 -DNDEBUG "${src}" -o "${bin_rel}"
}

build_san() {
  [[ -x "${bin_san}" && "${bin_san}" -nt "${src}" ]] && return 0
  "${cxx}" "${common[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    "${src}" -o "${bin_san}"
}

pin() {
  if command -v taskset >/dev/null 2>&1; then
    taskset -c 2 "$@"
  else
    "$@"
  fi
}

plugin_perf() {
  build_rel
  command -v perf >/dev/null 2>&1 || { echo "perf not found; skipping perf plugin" >&2; return 0; }
  local ev="instructions,cycles,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,dTLB-load-misses"
  printf '%-9s %10s %6s %9s %9s\n' workload cyc/op IPC br-miss L1-miss
  local w out cyc ipc brate lrate
  for w in "${workloads[@]}"; do
    out="$(pin perf stat -e "${ev}" "${bin_rel}" --workload "${w}" --ops "${ops}" --depth "${depth}" 2>&1)"
    cyc="$(printf '%s\n' "${out}" | sed -n 's/.* \([0-9.]*\) cyc\/op/\1/p')"
    ipc="$(printf '%s\n' "${out}" | sed -n 's/.*# *\([0-9.]*\)  *insn per cycle.*/\1/p')"
    brate="$(printf '%s\n' "${out}" | sed -n 's/.*# *\([0-9.]*\)% of all branches.*/\1/p')"
    lrate="$(printf '%s\n' "${out}" | sed -n 's/.*# *\([0-9.]*\)% of all L1-dcache.*/\1/p')"
    printf '%-9s %10s %6s %8s%% %8s%%\n' "${w}" "${cyc:-?}" "${ipc:-?}" "${brate:-?}" "${lrate:-?}"
  done
}

plugin_sanitize() {
  build_san
  echo "ASAN+UBSAN soak over the deep mix..."
  if pin "${bin_san}" --workload deep --ops "$((ops / 20))" --depth "${depth}"; then
    echo "sanitizer soak clean"
  else
    echo "SANITIZER FAULT (see above)" >&2
    return 1
  fi
}

plugin_record() {
  build_rel
  command -v perf >/dev/null 2>&1 || { echo "perf not found; skipping record plugin" >&2; return 0; }
  local data="/tmp/lob_profile_perf.data"
  if ! pin perf record -e task-clock -F 4000 -o "${data}" \
       "${bin_rel}" --workload deep --ops "${ops}" --depth "${depth}" >/dev/null 2>&1; then
    echo "perf record unavailable on this host; skipping" >&2
    return 0
  fi
  echo "top source lines by time (deep mix):"
  perf report -i "${data}" --stdio -n --sort=srcline 2>/dev/null | grep -vE '^#|^$' | head -16
}

case "${plugin}" in
  perf) plugin_perf ;;
  sanitize) plugin_sanitize ;;
  record) plugin_record ;;
  all)
    echo "== perf =="; plugin_perf
    echo "== sanitize =="; plugin_sanitize
    echo "== record =="; plugin_record
    ;;
  *) echo "unknown plugin: ${plugin}" >&2; exit 2 ;;
esac
