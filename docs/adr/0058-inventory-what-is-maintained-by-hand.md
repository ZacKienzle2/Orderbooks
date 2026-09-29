---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0058. Inventory what is maintained by hand

## Context and Problem Statement

Under the configuration rules, a version comes from a tool, a preset or constant
from a documented default or a source, and a helper from the library that
already does its job. ADR-0053 took the inventory of the C++ literals. This
record takes the rest with the tools' own reports, on `main` at the merge of
pull request 125, and proposes an owner for each group.

## Method

| Question                                 | Tool                                              |
| ---------------------------------------- | ------------------------------------------------- |
| Are the actions pinned to commits?       | `pinact run --check`                              |
| Are the hook revisions current?          | `prek update --check`                             |
| Are the Python locks current?            | `uv lock --check`, `uv tree --outdated --depth 1` |
| Is the vcpkg baseline current?           | `vcpkg x-update-baseline --dry-run`               |
| Which presets exist, and what repeats?   | `jq` over `CMakePresets.json`                     |
| Which blocks of code or YAML are copies? | jscpd 5.3.3 at its defaults, read with DuckDB     |
| Which literals lack a source?            | clang-tidy magic-number checks and ruff `PLR2004` |

## Findings

### Versions

| What                               | Where                                       | Updated by                         | Tool report                                    |
| ---------------------------------- | ------------------------------------------- | ---------------------------------- | ---------------------------------------------- |
| Action revisions                   | every workflow and the composite action     | Dependabot, `github-actions`       | pinact passes                                  |
| Hook revisions                     | `.pre-commit-config.yaml`                   | nothing since the cookie migration | prettier, ruff and check-jsonschema are behind |
| Python lock                        | `uv.lock`                                   | nothing since the cookie migration | hypothesis 6.168.1 against 6.168.3             |
| vcpkg baseline                     | `vcpkg-configuration.json`                  | nothing                            | behind the local vcpkg clone                   |
| Conda pins for the Friday workflow | `workflow/envs/*.pin.txt`                   | the workflow's own pin export      | two files share their first seven lines        |
| Runner images                      | `ubuntu-24.04` at 14 sites, `macos-14` at 2 | nothing                            | Dependabot does not update `runs-on`           |
| clang-tidy major version           | `clang-tidy-fix.yml` and `cmake.yml`        | nothing, typed in two files        | 18 in both                                     |
| Python versions                    | `ci.yml`                                    | the cookie template                | the template's matrix                          |

Dependabot documents `pre-commit`, `uv`, `vcpkg` and `conda` as package
ecosystems, and `.github/dependabot.yml` configures only `github-actions`. Pull
requests 81 and 109, which Dependabot opened for the uv group and the hook
revisions before the migration, are still open. The cookie template renders
`dependabot.yml`, and adding ecosystems there is a local edit that
`copier update` merges each time.

### Presets

`CMakePresets.json` defines 19 configure, 16 build, 15 test and 5 workflow
presets, all typed, and a hidden preset typed to share a value is typed as well.
`CMAKE_CXX_COMPILER=clang++` repeats in 9 presets, and `VCPKG_TARGET_TRIPLET`
restates the triplet vcpkg's toolchain detects when the variable is unset. vcpkg
doesn't generate presets, and these tools do.

| Tool                      | What it generates                                                                                                             | Cost                                                                                             |
| ------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| Conan 2, `CMakeToolchain` | Configure, build and test presets for each profile at `conan install`, prefixed `conan-`, with user presets that include them | The dependencies move from vcpkg to Conan, and workflow presets are not generated                |
| A Copier template for C++ | `CMakePresets.json` rendered from its questions, kept current by `copier update`                                              | The one found, 02XX/CMakeTemplate, has 8 commits and generates GoogleTest where this uses Catch2 |
| cmake-init                | Presets once, at creation                                                                                                     | No update (ADR-0057)                                                                             |

### Constants

The C++ literals are ADR-0053's. Ruff reports one `PLR2004` comparison, in
`bench_ci.py`, under an ignore the cookie template sets. The `justfile` types
operation counts, book depths, a sampling frequency and two perf event lists,
and the perf tutorial in the documentation server gives a sampling default the
`justfile` does not match. The perf-record manual is not indexed, so that
default is unconfirmed.

### Copies

jscpd found 64 clones. Test sources account for 35, and each test file states
its fixture next to its assertions. Outside the tests:

| Clone                                          | Sites                                                                              | Candidate                                                   |
| ---------------------------------------------- | ---------------------------------------------------------------------------------- | ----------------------------------------------------------- |
| A publisher whose every `publish` does nothing | seven definitions, in the profiler, two benchmarks, a fuzz harness and three tests | one type beside the publisher concept                       |
| The argument loop                              | the gateway, the load generator and the replay tool                                | CLI11 or Boost.Program_options, measured                    |
| Repeated blocks within one file                | the gateway 3, the profiler 3, the benchmarks 5                                    | read case by case                                           |
| The job prelude of harden-runner and checkout  | `cmake.yml`, `analysis.yml`, `bench.yml`, `clang-tidy-fix.yml`                     | a reusable workflow, which GitHub documents for shared jobs |

`.github/actions/cmake-preset` wraps configure and build in `run:` steps.
`lukka/run-cmake` is a published action that runs configure, build, test and
workflow presets, and its documentation is not yet indexed.

## Decision Drivers

- A version has a tool that writes it and a tool that moves it.
- One template writes a file, and a value is defined once.
- A copy is replaced by the library or platform feature that does its job, or by
  one definition when none does.

## Considered Options

- Configure the documented Dependabot ecosystems in the template-owned
  `dependabot.yml` and accept the merge on each `copier update`.
- Move version updates to Renovate, whose configuration no template renders.
- Leave the versions as they are and update them by running the tools.

## Decision Outcome

The owner chooses between the first two options, since both change what the
template renders or which bot the repository runs. The owner also chooses the
generator for the presets, since Conan replaces the package manager and a Copier
template adds a second template beside cookie. The rest follows the drivers and
doesn't need a decision. The restated triplets go, the no-op publisher becomes
one type, and `lukka/run-cmake` is measured against the composite action once
its documentation is indexed.

### Consequences

- Positive: each version and preset traces to the tool that writes it.
- Negative: generated presets need either a package manager change or a second
  template.
- Risk: a Dependabot ecosystem opens pull requests on its own schedule, and the
  hook and lock groups have already gone stale once.

## More Information

- Related: ADR-0053 (the C++ literals), ADR-0057 (cmake-init).
- Dependabot options reference, `package-ecosystem`,
  <https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-options-reference>
- vcpkg CMake integration, `VCPKG_TARGET_TRIPLET`,
  <https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration>
