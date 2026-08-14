---
name: bcp-feature
description: Use when a change is too large to land on master as one PR — cuts an empty integration branch feat/<name>, gets the plan reviewed as its own PR, then lands the work one task at a time as small PRs into that branch, watching each one and revising until the user merges it. Only the finished feature is proposed to master. Also resumes an existing feature and reports its state. Triggers: "bcp-feature X <prompt>", "start a feature branch for X", "continue the feature", "what's left on the feature", "land the feature".
---

# Running a Bernini feature branch

Small changes go straight to `master`. Anything large enough that master would be broken or
unreviewable in the middle of it gets an **integration branch**, `feat/<name>`, and arrives in it one
PR at a time. Only the finished feature is proposed to `master`.

## The loop

Everything is a PR into `feat/<name>` — the plan included. Every PR is watched. The user merges every
one.

```
bcp-feature <name> <prompt>
  │
  ├─ § 0  grill the request → consensus the user confirms  (nothing is created yet)
  ├─ § 1  cut feat/<name> from origin/master, empty, and publish it
  ├─ § 2  plan PR      → watch → revise → user merges
  ├─ § 3  task 1 PR    → watch → revise → user merges
  │       task 2 PR    → watch → revise → user merges       (one at a time)
  │       …
  └─ § 5  the whole feature → PR to master
```

| | |
|---|---|
| `bcp-feature <name> <prompt>` | start or resume `feat/<name>` |
| `bcp-feature <name>` | resume: report state, continue from where the tracker says |
| `bcp-feature` | status only |
| `bcp-feature --land` | § 5 |

## State

Every step below runs **in the current checkout**, which serves this feature and nothing else for
as long as the feature lives. How the checkout came to be is not this skill's concern: a plain
clone parks itself on `feat/<name>`; a setup that runs features in parallel prepares one linked
worktree per feature and starts the session inside it. Either way the checkout has its own
`./build`, its own tracker, its own Claude session.

Which feature a checkout serves — worktree-scoped, never committed:

```bash
git config extensions.worktreeConfig true           # once per clone; enables --worktree
git config --worktree bernini.feature feat/<name>
git config bernini.feature                          # read: unqualified; unset means "landing on master"
```

`--worktree`, not `--local`: local config is one file shared by every worktree of a clone, so two
live features would overwrite each other's key and send bcp-precheck diffing against the wrong base.
There is no unset step — the key dies with the checkout when a worktree is removed, and a plain
clone unsets it after § 5.

The **tracker**, `.claude/features/<name>.md` (git-ignored). The only record that survives the
session, so rewrite it after every state change — a stale one sends the next session to redo merged
work:

```markdown
# feat/culling — <one line: what the feature delivers>
plan: docs/plans/culling.md

- [x] the plan — #111, merged
- [x] frustum planes on the camera — #112, merged
- [>] per-instance cull compute pass — #118, in review
- [ ] cull results feed the indirect draw
```

`[ ]` pending, `[>]` PR open, `[x]` merged.

## 0. Grill before you cut

**Nothing is created until the request has been grilled** — not the branch, not the tracker, not the
plan. Run [bcp-grill](.claude/skills/bcp-grill/SKILL.md) on the prompt and close on a consensus the
user has confirmed. With nobody at the keyboard it stops and waits rather than answering itself.

It runs ahead of § 1 because it is allowed to conclude that this feature should not exist: that the
work is one PR to `master` after all ([bcp-implement](.claude/skills/bcp-implement/SKILL.md)), that
it duplicates something already in `core`, or that the premise did not survive the survey. A branch
cut first is a branch that then has to be explained away.

Its output is the head of § 2's plan — Context, Decisions, Non-goals, Acceptance — so the feature's
boundaries are agreed before its decomposition is invented, rather than discovered by reviewing one.

On a **resume**, the grill already happened and its consensus is the head of
`docs/plans/<name>.md`. Read it; do not re-grill. Re-grill only the point a task disproved, and
amend that ADR in the task's own PR (§ 3).

## 1. Cut the branch

```bash
export PATH="$PATH:/c/Program Files/GitHub CLI"   # gh is often not on PATH
git fetch origin
```

The checkout must be clean — `git status` empty — so nothing half-done leaks into the feature's
PRs. Cut the branch from `origin/master` and publish it:

```bash
git switch -c feat/<name> origin/master     # skip when the checkout is already on feat/<name>
git push -u origin feat/<name>
git config extensions.worktreeConfig true
git config --worktree bernini.feature feat/<name>
```

It starts **empty**, identical to `master`, and changes only when a PR merges. **Nothing is ever
committed on it directly.**

If `feat/<name>` already exists, this is a **resume**: fast-forward, read the plan and the tracker,
and reconcile the tracker with reality before anything else — a `[>]` may have merged since.

```bash
gh pr list --base feat/<name> --state all --json number,title,state
just watch-pr <n> --once      # per open PR: merge state, failing checks, reviews and comments
```

### Naming

| | |
|---|---|
| Feature branch | `feat/<name>` — `feat/culling`, `feat/drop-webgpu` |
| Plan branch | `docs/<name>-plan` |
| Task branch | `<type>/<name>-<slug>` — `feat/culling-frustum-pass`, `fix/culling-aabb-sign` |

Task branches are **siblings** of the feature branch, never children: git stores refs as paths, so
once `feat/culling` exists no ref may begin `feat/culling/`. The push fails with
`cannot lock ref ... exists; cannot create`, which does not explain itself.

## 2. The plan, as its own PR

Read the code first — the docs the change touches (index in [CLAUDE.md](CLAUDE.md)) and the real
source. A decomposition invented from the prompt text splits along the words rather than the seams,
and every task then fights the last. [`bcp-docmap`](.claude/agents/bcp-docmap.md) does the docs half
of that survey — spawn it with `subagent_type: bcp-docmap`, one tier below your own model, and it
returns the answer plus the lines
it rests on, which is what the plan's *what the survey found* section wants anyway.

Write `docs/plans/<name>.md`. The first four sections are § 0's confirmed consensus and the rest is
the working-out beneath it — the order matters, because a reader who disagrees with the boundaries
should find that out before reading the decomposition that assumes them:

- **Context** — what breaks today, and why now.
- **Decisions** — the ADRs, each with its reason and *the alternative rejected*. A decision with no
  rejected alternative recorded is one the next reader re-litigates. Decisions the plan makes that
  the grill did not reach get the same one-line treatment; one that crosses a stated non-goal goes
  back to § 0 instead of into this list.
- **Non-goals** — what the feature is explicitly not doing. [`bcp-precheck`](.claude/agents/bcp-precheck.md)
  § 4 reads every task's diff against these, so a boundary written vaguely is a boundary that does
  not hold.
- **Acceptance** — the gate that proves the feature as a whole.
- **What the survey found** — the state of the code the feature must work with, as facts with file
  references. This is what stops the next session re-reading the same forty files.
- **What changes**, per file or subsystem, and what could break.
- **The tasks in order, each with the gate that proves it** — the suite, the golden image, the
  assertion. "It builds" is not a gate.

It records reasoning and the shape of what does not exist yet — not a mirror of the code, which is
what the source and `docs/` are for. Follow [bcp-docs](.claude/skills/bcp-docs/SKILL.md) for prose.
It lives only as long as the feature: § 5 deletes it, once whatever should outlive the feature has
moved into `docs/`.

```bash
git switch -c docs/<name>-plan feat/<name>
git add docs/plans/<name>.md
git commit -m "docs(plans): plan <what the feature delivers>"   # the hook adds the bot co-author
# spawn bcp-precheck here (§ 3), and act on it before pushing
git push -u origin HEAD
just pr create --base feat/<name> --body-file <file>
```

The plan is where a design finding is cheapest of all — it costs a paragraph here and a rewrite once
the tasks have landed.

Then § 4. **No task branch is cut until this PR merges** — the plan fixes what every later PR is
measured against, and a decomposition reviewed after three tasks have landed is reviewed too late to
change anything cheaply.

### What a task is

One PR. It must:

- **build and pass on its own.** Never "part 1 of 2, compiles after part 2" — a reviewer bisects these.
- **be reviewable in one sitting.** If it needs an "and" to describe, it is two.
- **rest on what came before.** Bottom-up by layer: `bgl`, then `assetlib`, then `gamelib`, then
  `apps/editor`, because that is the direction the dependencies point. A refactor that enables the
  feature is its own task, ahead of the feature.
- **be worth reviewing alone.** Do not split a 20-line change into three PRs to look incremental.

Dead scaffolding is the one thing that justifies a task landing unused: a `bgl` task may add an
interface nothing calls yet, provided the tests call it. Say so in the PR body.

## 3. One task

One at a time. Always branch from `origin/feat/<name>` — never the local branch (stale), never
whatever is checked out (drags in unrelated work):

```bash
git fetch origin
git switch -c <type>/<name>-<slug> origin/feat/<name>
```

Then follow [bcp-implement](.claude/skills/bcp-implement/SKILL.md) §1–§7 as written: read first,
commit boundaries decided before coding, STYLE.md (a better name beats a comment), tests that pin
behaviour, build, **read the logs**, docs updated in the same commit.

Two things apply only inside a feature:

- **A task does not re-grill.** § 0 grilled the feature; the reference is
  [bcp-implement](.claude/skills/bcp-implement/SKILL.md) **§1–§7**, not § 0. The exception is a task
  that disproves an ADR — grill that one point, and amend the plan in this task's PR.
- **Depend on what already landed on `feat/<name>`, not on master.** The helper an earlier task added
  is in `origin/feat/<name>`. Do not write a second one.
- **Hold the task boundary.** Work belonging to a later task goes in that task, even when it is three
  lines and right there. Scope creep is what recreates the wall of diff this branch exists to avoid.

Verify, format, open the PR:

```bash
just build && just test                      # or the suites the change touches
just run bgl_tests -- --gpu-validation       # if it touches shaders, barriers or descriptors
just format <files...>
git fetch origin && git rebase origin/feat/<name>   # the base moved if a sibling merged
just build && just test                             # again — a rebase is a real merge
# spawn bcp-precheck here, and act on it before pushing
git push -u origin HEAD
just pr create --base feat/<name> --body-file <file>
```

**Every PR this skill opens is read by [`bcp-precheck`](.claude/agents/bcp-precheck.md) first**, the
plan's and § 5's included. Spawn it with the Agent tool, `subagent_type: bcp-precheck`, one tier below
your own model, after the last verification step and before the push — § 2 has no rebase to hang it
on, § 5 needs its base named explicitly. It reads the diff against the base for code that already
exists in `core`, a design that fights `ROADMAP.md`, work that crosses a non-goal or contradicts an
ADR in the plan, and `STYLE.md` breaks. A `block` verdict means fix and re-run; the PR does not open
on one. See [bcp-implement § 8](.claude/skills/bcp-implement/SKILL.md) for the full loop.

`bernini.feature` is what tells the precheck its base, so a slice reviewed while that config is unset
gets diffed against `master` and reports the whole feature. § 1 sets it; check it is still set.

`--base` is not optional, and it is not defaulted: name the feature branch or the PR proposes the
work to `master`. The body goes in a file, headed by `# type(scope): the title` — the title is lifted
from that line, since `just` joins a recipe's arguments on spaces. `just pr create` opens the PR as
**you**, not as the bot -- a squash merge takes the commit's author from the PR's author, and a
feature squashes twice -- and it arms the watch that § 4 must clear.

The body says what changed, **why**, how it was verified (name the suites; say whether GPU validation
ran), which task of the plan it is, and what still has to land. A reviewer must be able to tell a
deliberate gap from an oversight.

The plan is a hypothesis. When a task disproves it, correct `docs/plans/<name>.md` **in that task's
PR**, so the correction is reviewed beside the code that forced it.

## 4. Watch, revise, wait for the merge

Applies to every PR this skill opens, the plan's included.

**Report to the user in chat** — the PR, what it contains, what is next. Then start the watcher **in
the background**, as the last action of the turn:

```bash
just watch-pr <n>        # python scripts/watch_pr.py <n>  -- run it in the background
```

Run it in the foreground and the session is parked until the PR moves: the user cannot type, and
reaching you means killing the wait. Backgrounded, the turn ends, they keep their session, and the
event wakes you when it arrives. In Claude Code that is the Bash tool's `run_in_background`.

**It waits indefinitely, and that is the point.** The watcher only exits on something you can act
on. Do not give it a `--timeout` to "check in" with: a timeout wakes the session to report that
nothing happened, re-arms the watchlist, and makes the Stop hook demand another watcher — an hourly
cycle that notifies the user every lap and never converges.

This is not optional and not remembered: `just pr create` records the PR, and the `Stop` hook refuses
to end the turn until a watcher is running on it. The watcher claims the PR as it starts, so the hook
is satisfied while it runs rather than only after it exits. If the user has to decide something
before it can be watched, say so and release it with `just pr unwatch <n>`.

**One watcher per PR, and it is the script that guarantees it** — started again on a PR someone is
already watching, `just watch-pr` names the pid holding it and exits. So a turn that posts a reply
while a watch is running needs no new watcher: the running one polls the PR and will report whatever
arrives next. Start one when the hook asks for one, and take the refusal as the answer rather than
working around it.

It baselines the PR's current activity, polls, and blocks until something actionable happens, printing
one JSON event. Do not poll `gh` yourself while it runs — it is the wait, not a hint, and that includes
`gh pr checks`. Only *submitted* reviews fire it; a reviewer's pending draft stays invisible until they
send it.

| event | do |
|---|---|
| `merged` | mark `[x]` in the tracker; next task (§ 3), or § 5 if that was the last |
| `ci_failure` | fix the build on the same branch, push, restart the watcher — see below |
| `review` / `comment` | [bcp-revise](.claude/skills/bcp-revise/SKILL.md) on the same branch, push more commits, restart the watcher |
| `base_moved` | rebase onto the base it names, rebuild, re-test, force-push, restart the watcher — see below |
| `closed` | stop and ask — the user rejected something |
| `timeout` (exit 3) | only when you passed `--timeout`, which you should not — say you are still waiting, restart the watcher |

**A red build is fixed before the review is answered.** Each failed check in a `ci_failure` event
carries the diagnostics lifted out of its job log, so the compiler errors are already in hand — read
them rather than opening the `url`. Reproduce locally where the runner's toolchain allows it
(`just build --preset windows-ninja-msvc-dx12-debug`), fix, push, and restart the watcher. Where it
does not — a warning only MSVC emits, a macOS-only failure — say in chat that the fix is unverified
locally and that the next CI run is the gate. The event also carries any review or comment that was
waiting; answer those in the same turn, then restart the watcher once.

**A moved base is a rebuild, not a rebase.** `base_moved` says the branch this PR merges into has
commits the PR was never compiled against, so the green CI run it is sitting on was measured against
a base that no longer exists. GitHub will still merge it — being behind blocks nothing — which is
exactly why nothing else reports this:

```bash
git fetch origin && git rebase origin/<the event's base>
just build && just test                             # a rebase is a real merge
git push --force-with-lease
```

Then restart the watcher, which re-baselines the base as it starts. A conflict here is the same
conflict a hand rebase would have hit, only earlier; resolve it, and if the resolution is a judgement
call rather than a mechanical one, say so in chat rather than picking a side silently. The force-push
marks any inline review comment as outdated, which is why the event ranks below `review` and
`comment` — answer the human first, and their answer carries the rebase anyway.

**Just restart it.** The baseline is not yours to compute:

```bash
just watch-pr <n>
```

`just pr` records the timestamp GitHub returned for everything it posts, and the watcher starts from
that, so your own reply is already behind the baseline and anything that arrived while you were
revising is still ahead of it. `--since` remains for the rare case of overriding that by hand — a
timestamp guessed a second early makes the watcher fire on your own reply and spends the turn
reading yourself.

**Answer an inline comment inside its thread.** An event whose `path` is set came from a review
thread anchored to a file and line, and it carries the `replyTo` id to answer under:

```bash
just pr reply {replyTo} --body-file <file>   # in-thread; the id decides, not you
just pr comment {n} --body-file <file>       # the summary, once every thread is answered
```

Both post as the bot. Answering an inline comment at the bottom of the conversation leaves the
reviewer's thread unresolved and puts the answer where the question is not, so `just pr comment`
refuses while any thread is unanswered, and raw `gh pr comment` is blocked outright.

**Do not write to a PR that has no review on it.** Reporting goes to the user in chat; a description
of your own change belongs in the PR **body** (`just pr edit {n} --body-file <file>`), not a comment.
Comments answer review feedback.

## 5. Land the feature

When the tracker is all `[x]`:

```bash
git fetch origin && git switch feat/<name> && git rebase origin/master
just build && just test                      # the whole feature at once
just run bgl_tests -- --gpu-validation       # if any task touched shaders, barriers or descriptors
# spawn bcp-precheck here, against origin/master -- see below
git push --force-with-lease
just pr create --base master --head feat/<name> --body-file <file>
```

This run matters more than any single task's did: each was verified against the branch as it stood at
the time, and this is the first time all of them exist together. The critical read is the same: it is
the first time anyone reads the feature as one diff.

**Tell it the base explicitly here.** `bernini.feature` is still set until the merge, so a precheck
left to resolve its own base would diff against `origin/feat/<name>` — the stale remote ref, whose
merge-base with the just-rebased branch is where the feature was cut. It would report the feature
plus every unrelated `master` commit since. The base for this one is `origin/master`.

The body is the feature's story — what it adds, why, how it was verified as a whole, what was
deliberately left out. Link the task PRs; do not restate them. Then § 4 again.

Read the plan against what actually shipped first: anything it still promises that the feature did
not do is a correction for the last task PR. Whatever in it should outlive the feature — how the
code now *behaves*, the decisions worth keeping — belongs in a subsystem page under `docs/`: move it
there with [bcp-docs](.claude/skills/bcp-docs/SKILL.md), then **delete the plan** — by PR into
`feat/<name>` like everything else, the feature's last, so the landing PR carries the deletion.
A plan is scaffolding for the feature's review, not documentation; once the feature lands,
`docs/*.md` is the record, and a kept plan is a second source of truth waiting to disagree.

`master` moves under a long feature. Rebase onto it when it drifts, and only when **no PR is open** —
a rebase rewrites the commits those PRs are based on, and each then shows a diff nobody wrote. With
PRs open, merge them first.

After the feature merges, the checkout's job is done. In a plain clone:

```bash
git switch master && git pull && git branch -d feat/<name>
git config --worktree --unset bernini.feature
rm .claude/features/<name>.md
```

A checkout that exists only for this feature — a linked worktree — is instead torn down whole by
whatever created it, and the worktree config and the git-ignored tracker die with it. The plan
needs no cleanup either way: it went with the landing PR.

## Rules

- **Never merge a PR.** Continuous review is the entire point, and it only works if a human approves.
- **Never cut the branch before the grill closes.** § 0 comes first, and it may end the feature.
- **Never commit directly onto `feat/<name>` or `master`.** Everything arrives by PR.
- **Never cut a task branch before the plan PR has merged.**
- **Never let a task quietly cross a non-goal.** Either it goes back to the grill and the ADR is
  amended in that PR, or the work is cut.
- **Never open a PR without `--base`.** The default is `master` and it will be wrong.
- **Never let a task land broken.** "Fixed in the next PR" defeats bisecting and wastes the review.
- **Never claim a test passed without running it.** If a step was skipped, say so.
- **Keep the tracker true.**
- **Push back** — if the work does not need a feature branch, say so; one PR to `master` is cheaper.
  If a task is too big to review, split it. If the prompt breaks a layering rule or a documented
  invariant, argue rather than comply.
