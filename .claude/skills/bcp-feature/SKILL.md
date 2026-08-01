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

Which feature is active — local to this clone, never committed:

```bash
git config --local bernini.feature feat/<name>
git config --local bernini.feature                  # read it; unset means "landing on master"
git config --local --unset bernini.feature
```

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

## 1. Cut the branch

```bash
export PATH="$PATH:/c/Program Files/GitHub CLI"   # gh is often not on PATH
git fetch origin
```

The tree must be clean — a branch cut from a dirty tree drags unrelated work into every PR based on
it.

```bash
git switch -c feat/<name> origin/master
git push -u origin feat/<name>
git config --local bernini.feature feat/<name>
```

It starts **empty**, identical to `master`, and changes only when a PR merges. **Nothing is ever
committed on it directly.**

If it already exists, this is a **resume**: check it out, fast-forward, read the plan and the tracker,
and reconcile the tracker with reality before anything else — a `[>]` may have merged since.

```bash
gh pr list --base feat/<name> --state all --json number,title,state
just watch-pr <n> --once      # per open PR: merge state, plus every submitted review and comment
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
and every task then fights the last.

Write `docs/plans/<name>.md`:

- **What the survey found** — the state of the code the feature must work with, as facts with file
  references. This is what stops the next session re-reading the same forty files.
- **Each design decision, with its reason and the alternative rejected.** A decision with no rejected
  alternative recorded is one the next reader re-litigates.
- **What changes**, per file or subsystem, and what could break.
- **The tasks in order, each with the gate that proves it** — the suite, the golden image, the
  assertion. "It builds" is not a gate.

It records reasoning and the shape of what does not exist yet — not a mirror of the code, which is
what the source and `docs/` are for. Follow [bcp-docs](.claude/skills/bcp-docs/SKILL.md) for prose.

```bash
git switch -c docs/<name>-plan feat/<name>
git add docs/plans/<name>.md
git commit -m "docs(plans): plan <what the feature delivers>"   # the hook adds the bot co-author
git push -u origin HEAD
just pr create --base feat/<name> --body-file <file>
```

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
git push -u origin HEAD
just pr create --base feat/<name> --body-file <file>
```

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

Run it in the foreground and the session is parked for up to an hour: the user cannot type, and
reaching you means killing the wait. Backgrounded, the turn ends, they keep their session, and the
event wakes you when it arrives. In Claude Code that is the Bash tool's `run_in_background`.

This is not optional and not remembered: `just pr create` records the PR, and the `Stop` hook refuses
to end the turn until a watcher is running on it. The watcher claims the PR as it starts, so the hook
is satisfied while it runs rather than only after it exits. If the user has to decide something
before it can be watched, say so and release it with `just pr unwatch <n>`.

It baselines the PR's current activity, polls, and blocks until something actionable happens, printing
one JSON event. Do not poll `gh` yourself while it runs — it is the wait, not a hint. Only *submitted*
reviews fire it; a reviewer's pending draft stays invisible until they send it.

| event | do |
|---|---|
| `merged` | mark `[x]` in the tracker; next task (§ 3), or § 5 if that was the last |
| `review` / `comment` | [bcp-revise](.claude/skills/bcp-revise/SKILL.md) on the same branch, push more commits, restart the watcher |
| `closed` | stop and ask — the user rejected something |
| `timeout` (exit 3) | say you are still waiting, restart the watcher |

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
git push --force-with-lease
just pr create --base master --head feat/<name> --body-file <file>
```

This run matters more than any single task's did: each was verified against the branch as it stood at
the time, and this is the first time all of them exist together.

The body is the feature's story — what it adds, why, how it was verified as a whole, what was
deliberately left out. Link the task PRs; do not restate them. Then § 4 again.

Read the plan against what actually shipped first: anything it still promises that the feature did not
do is a correction for the last task PR. Whatever in it describes how the code now *behaves* belongs
in a subsystem page under `docs/` — move it there with
[bcp-docs](.claude/skills/bcp-docs/SKILL.md) and leave the plan holding the reasoning.

`master` moves under a long feature. Rebase onto it when it drifts, and only when **no PR is open** —
a rebase rewrites the commits those PRs are based on, and each then shows a diff nobody wrote. With
PRs open, merge them first.

After the feature merges:

```bash
git config --local --unset bernini.feature
git switch master && git pull && git branch -d feat/<name>
rm .claude/features/<name>.md          # the tracker only; the plan lives on in docs/plans/
```

## Rules

- **Never merge a PR.** Continuous review is the entire point, and it only works if a human approves.
- **Never commit directly onto `feat/<name>` or `master`.** Everything arrives by PR.
- **Never cut a task branch before the plan PR has merged.**
- **Never open a PR without `--base`.** The default is `master` and it will be wrong.
- **Never let a task land broken.** "Fixed in the next PR" defeats bisecting and wastes the review.
- **Never claim a test passed without running it.** If a step was skipped, say so.
- **Keep the tracker true.**
- **Push back** — if the work does not need a feature branch, say so; one PR to `master` is cheaper.
  If a task is too big to review, split it. If the prompt breaks a layering rule or a documented
  invariant, argue rather than comply.
