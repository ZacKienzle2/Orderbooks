---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0061. Gate merges on every workflow run

## Context and Problem Statement

main's ruleset requires one status check, `pass`, with branches kept up to date.
`pass` is the alls-green job of the template's `ci.yml`, and it gathers the jobs
of that workflow alone. The build matrix, clang-tidy and include-what-you-use
run in `cmake.yml`, cppcheck and Doxygen in `analysis.yml`, and the fuzzing and
benchmark jobs in `fuzz.yml` and `bench.yml`. GitHub's auto-merge waits for the
required checks only, so pull request 130 merged while its clang-tidy and
include-what-you-use jobs were still running. The owner's rule is that a pull
request merges after every job has finished.

## Considered Options

- Require every check of the other workflows by its name in the ruleset.
- Move those jobs under `pass` in `ci.yml`.
- Require one job that waits for every other check run of the head commit, with
  `poseidon/wait-for-status-checks`.
- Require one job that waits for every other workflow run of the head commit,
  with `int128/wait-for-workflows-action`.

## Decision Outcome

This record proposes **the fourth option**. The gate's job is defined in
`.github/workflows/checks.yml`, and main's ruleset would require it as
`wait-for-workflows` alongside `pass`.

The action reads the check suites of the head commit that the GitHub Actions app
owns, one per workflow run, and waits until every run has completed or one has
failed. Its README says it "excludes the workflow of self", and the source
compares each run's workflow name with `github.workflow`, so the job's own name
plays no part. A workflow run is complete only once its last job has finished.
On pull request 129 the CMake run's jobs API shows clang-tidy and
include-what-you-use created at 06:24:21, one second after the last matrix job
finished, and the CMake check suite updated at 06:28:25, when
include-what-you-use finished. A workflow that a paths filter never started has
no run, so it can't hold the job open.

The action's own defaults stand. `initial-delay-seconds` is 10, and the four
`pull_request` runs of pull request 129 were all created at 06:19:07, within one
second of each other. The one input set is `filter-workflow-events`, because
CodeQL's default setup runs under the `dynamic` event, which the default of
`github.event_name` leaves out. The job's permissions are `actions: read` and
`checks: read`, for the workflow runs and the check suites of the query. The
pull request that adds the file is the first run of the gate.

`pass` stays required. It is the check the template's `ci.yml` builds for a
ruleset, and removing it is a separate change to the template's contract.

The first option fails for two documented reasons. `fuzz.yml` and `bench.yml`
start only for matching paths, and GitHub's page on troubleshooting required
status checks says a required check from a workflow that a paths filter skipped
stays pending and blocks the merge. Requiring only the unfiltered workflows
means naming the eight matrix jobs, include-what-you-use, clang-tidy, cppcheck
and Doxygen, and any change to the matrix renames checks in that list. The
alls-green README describes that list as the maintenance its action exists to
remove.

The second option fails too. `ci.yml` comes from `gh:scientific-python/cookie`,
which `.copier-answers.yml` records, and editing it by hand puts the file at
odds with the template on the next `copier update`. Calling the other workflows
from it as reusable workflows doesn't help either, because `on.workflow_call`
takes inputs, outputs and secrets and has no paths filter. The fuzzing and
benchmark workflows would then run for every pull request, or stay outside the
gate.

The third option watches check runs, one per job, and has two recorded faults.
It leaves out its own run by comparing check run names with `github.job`, which
GitHub's contexts reference defines as the job's id, so a job with a `name`
waits on itself until the timeout (open issue 303). A check run exists only once
its job is created, and the jobs of `cmake.yml` that need the build matrix are
created after it finishes. A poll made during that gap finds every existing
check run completed and passes, which is the report of issue 625, closed with
the advice to set `delay`. No delay covers a gap that opens after the first
poll.

### Consequences

- Positive: every merge waits for the last job of each workflow run that the
  pull request started, CodeQL included.
- Positive: the required list is two names that no matrix change touches.
- Negative: the gate's job occupies a runner while it polls, which GitHub
  doesn't charge for on a public repository's standard runners.
- Negative: the ruleset change is a repository setting that no file here
  records, so this record states it.

## More Information

- GitHub, troubleshooting required status checks, "Handling skipped but required
  checks".
- GitHub, contexts reference, `github.job`.
- `re-actors/alls-green` README, "Why?".
- `int128/wait-for-workflows-action` README, "How it works" and "Specification",
  and `src/checks.ts` at v1.104.0.
- `poseidon/wait-for-status-checks` issues 303 and 625, and `src/main.ts` and
  `src/poll.ts` at v0.7.0.
