# Configuration index

Every configuration file here, the tool that owns it, the documentation that
defines it, and the command that prints what the tool actually decided. A
setting nobody can explain is a guess, and a file that disagrees with its tool
is a defect that shows up somewhere else.

Ask the tool before editing a file in this index. Each of the commands below
reports the effective configuration, packages and defaults included, which is
not the same as the file's contents.

## Who owns what

Files marked template come from `ZacKienzle2/repo-template` through
`copier update`. A fix to one of those belongs upstream, because an edit here
has to be merged again on every update. `.copier-answers.yml` records which
template commit produced them.

| File                      | Tool              | Owner      | Effective configuration                         |
| ------------------------- | ----------------- | ---------- | ----------------------------------------------- |
| `.clang-format`           | clang-format      | template   | `clang-format --dump-config`                    |
| `.clang-tidy`             | clang-tidy        | repository | `clang-tidy --explain-config`, `--list-checks`  |
| `.cmake-format.json`      | cmake-format      | template   | `cmake-format --dump-config yaml`               |
| `.copier-answers.yml`     | copier            | generated  | `copier` rewrites it; never edit by hand        |
| `.editorconfig`           | editorconfig      | template   | `editorconfig-checker`                          |
| `.gitattributes`          | git               | template   | `git check-attr -a <path>`                      |
| `.gitignore`              | git               | template   | `git check-ignore -v <path>`                    |
| `.markdownlint-cli2.yaml` | markdownlint-cli2 | template   | `markdownlint-cli2 --help` for resolution order |
| `.pre-commit-config.yaml` | pre-commit        | template   | `pre-commit validate-config`                    |
| `.prettierrc.yaml`        | prettier          | template   | `prettier --find-config-path <file>`            |
| `.python-version`         | uv                | template   | `uv python find`                                |
| `.vale.ini`               | Vale              | template   | `vale ls-config`                                |
| `.yamllint.yaml`          | yamllint          | template   | `yamllint --list-files .`                       |
| `CMakePresets.json`       | CMake             | repository | `cmake --list-presets=configure`                |
| `_typos.toml`             | typos             | template   | `typos --dump-config -`                         |
| `cliff.toml`              | git-cliff         | template   | `git-cliff --context`                           |
| `commitlint.config.mjs`   | commitlint        | template   | `commitlint --print-config`                     |
| `lychee.toml`             | lychee            | template   | `lychee --dump`                                 |
| `mutation.toml`           | cosmic-ray        | template   | read by `nox -s mutants`                        |
| `pyproject.toml`          | many              | template   | `ruff check --show-settings`, `repo-review .`   |
| `vcpkg.json`              | vcpkg             | repository | `vcpkg install --dry-run`                       |

## What the file does not say

Each of these cost a debugging session. They are properties of the tool that the
configuration file gives no sign of.

**Vale layers packages ahead of the file.** `Packages` entries are downloaded by
`vale sync` into `StylesPath`, and Vale reads each package's own `.vale.ini`
before the rendered one. The checks that run are therefore not the ones
`BasedOnStyles` names here: `vale ls-config` on this repository reports
`Google.*` and `write-good.*` checks and a `tex` format mapping that appear
nowhere in `.vale.ini`, because `ai-tells` brings them. `StylesPath` is resolved
relative to the file that sets it and has to exist when Vale runs, which is why
the `vale sync` hook runs before the `vale` hook.

**taplo reflows an array by line width, in both directions.** `column_width`
defaults to 80, and `array_auto_expand` and `array_auto_collapse` both default
to true, so taplo splits a long single-line array and collapses a short
multi-line one. Anything generated into a TOML file therefore matches the
formatter only at some lengths. Per-glob overrides go in `.taplo.toml` under
`[[rule]]`, which this repository does not have.

**zizmor takes its threshold from the command line alone.** `.zizmor.yml` holds
`rules.<id>.ignore` with `filename[:line[:col]]` entries and no glob support,
and it has no `min_severity` key. The threshold is passed to the pre-commit hook
and again to the action in `zizmor.yml`, which is why both places carry a
comment naming the other. Without it on the action, a low finding the hook is
configured to accept still reaches code scanning, where it opens a review thread
and blocks the merge.

**clang-tidy lints a compilation database, not a directory.** A target behind a
CMake option is absent from `compile_commands.json` unless the configure step
enables it, and clang-tidy then guesses a command carrying the build type's
`NDEBUG`. That deletes the assertions a fuzz harness is written around and
reports every variable they check as unused. The clang-tidy job passes
`-DLOB_BUILD_FUZZ=ON` for this reason.

**copier renders the recorded `_commit`, not the working tree.** Editing
`template/` and running `copier recopy` renders the old sources and looks like
the change did nothing. `--vcs-ref HEAD` includes uncommitted template changes
and prints `DirtyLocalWarning`. A conflict from `copier update` leaves real git
merge stages, so `git checkout --theirs <file>` takes the template's side.

**An answer copier never asked has no record, and `--defaults` takes its
default.** `is_public` defaults to false, and running the update without it
deleted fourteen workflows here with no message. Check `git status` for
deletions before committing an update.

**A `paths:` filter is silence, not a failure.** A workflow whose filter omits
one of the build's directories does not run, nothing reports it, and a green
pull request means only that nothing was built. PR #56 was that for `apps/**`,
and `fuzz/**` was the same hole again. actionlint validates a filter's glob
syntax and no tool checks whether one covers the build, so the filter is a list
kept by hand. `cmake.yml` therefore has no filter at all: it is the correctness
gate and runs on every pull request. The expensive and scheduled workflows keep
theirs, where a missed run costs a measurement rather than a check.

**deptry reads a rendered `noxfile.py` as a run-time import.** The noxfile is
the template's and imports its own dev-group dependencies, so `DEP004` fires on
every repository the template renders. `[tool.deptry.per_rule_ignores]` in
`pyproject.toml` records it.

**repo-review reads the working tree, not the git index.** An untracked or
ignored directory satisfies a directory check locally and fails it on a fresh
checkout, and git stores no empty directory, so each of those checks needs a
tracked file inside. `PY004` asks for `docs/`, `PY005` for a test directory,
`GH100` for `.github/workflows/`, `GH200` for `.github/dependabot.yml`.

## Asking a tool what it selected

A list of checks written into a document is a copy that nothing regenerates.
These commands print the live answer.

```bash
ruff linter                       # every rule set, and what each implements
ruff check --statistics           # what fired here, under this selection
ruff rule <code>                  # one code's meaning and rationale
clang-tidy --list-checks          # what this repository's .clang-tidy enables
clang-tidy --explain-config       # which file turned a check on
uvx --from 'sp-repo-review[cli]' repo-review .
```

Every preset in `CMakePresets.json` carries a host condition, so
`cmake --list-presets=configure` prints nothing on a machine none of them match.
That is the presets working, not a missing file.
