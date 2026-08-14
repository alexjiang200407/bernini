# watch-base-moved — implementation plan

## Context

`watch_pr.py` reports every way a pull request can move except the one that invalidates the result
it just reported: a commit on the base branch. A slice PR into `feat/<name>` falls behind each time
a sibling merges, and a `ci_failure`-free poll keeps saying the PR is green against a base it was
never built on. Today the drift is found only when someone rebases by hand — on a feature branch,
after the conflict is already old.

## Decisions

- **ADR-1 — the watcher emits a `base_moved` event; it does not rebase.** The agent answers it the
  way it answers `ci_failure`: rebase, rebuild, re-test, push, restart the watch — which keeps
  bcp-feature's rule that a rebase is a real merge and must be built. *Rejected: rebasing inside the
  watcher, because it runs detached in the background and would mutate a checkout its session is
  still working in, and push a merge nothing compiled. Rejected: GitHub's `update-branch` API,
  because a server-side rebase leaves the local branch diverged from the remote.*
- **ADR-2 — it fires on any movement of the base tip**, baselined when the watcher starts.
  *Rejected: firing only on `mergeable == CONFLICTING` or a blocked merge, because GitHub does not
  block a squash-merge for being behind — so the stale CI result, which is the thing being caught,
  is invisible to it.* The cost is accepted: on a busy `feat/<name>` this wakes the session per
  sibling merge, and each force-pushed rebase marks a reviewer's inline comments outdated.
- **ADR-3 — the base tip is read from `git/ref/heads/<base>`.** *Rejected: `baseRefOid` from
  `gh pr view`, because on a merged PR it returns the oid the base had at merge time, so it is not
  demonstrably a live signal.*
- **ADR-4 — `base_moved` ranks below `ci_failure`, `review` and `comment`, and re-arms the watch.**
  *Rejected: ranking it above them, because a human waiting on an answer outranks mechanical drift,
  and answering them rebases anyway.*

## Non-goals

- The watcher never rebases, never pushes, and never touches a working tree.
- No test infrastructure for `watch_pr.py`. The script is a thin shell over `gh`, and stubbing it
  would be most of the work for a suite this repo has nowhere to run.
- No compare call, so the event says the base moved, not by how many commits.
- No change to `pr.py`, to the watchlist format, or to which base a PR targets.

## Companion change (bernini-workspace)

Agreed in the same grill, landing in the other repo because that is where the script lives:

- **ADR-5 — `ws done` prompts `y/N` on every run, merged or not**, listing the worktree, the branch,
  the tmux window and whether the branch is merged; `--yes` and a non-tty skip it. *Rejected: a
  warning with no prompt, because printing into a scrollback nobody is reading is not a warning.*
  Its destructive steps keep their current order — the worktree still goes before `git branch -d`
  discovers an unmerged branch — so the prompt is what tells you first.

## Acceptance

Manual, with the evidence quoted in the PR body. There is no suite to add to: `just test` discovers
CMake `*_tests` targets, and no Python script here has ever had a test.

- A watcher on a live PR, a commit pushed to its base, and the `base_moved` JSON on stdout.
- `--once` on the same PR reporting the base ref and tip.

## Commits

1. `docs(plans): plan an event for a base branch that moves under a PR` — this file.
2. `feat(scripts): wake the agent when a PR's base branch moves` — the event, the base-tip poll, the
   docstring. Gate: the manual run above.
3. `docs(skills): answer base_moved with a rebase` — the `bcp-feature` § 4 event table and the
   paragraph under it. Gate: read-back against § 4's existing `ci_failure` wording.
