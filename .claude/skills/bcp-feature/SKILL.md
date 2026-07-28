---
name: bcp-feature
description: Use when a change is too large to land on master as one PR — takes a feature branch name and a prompt, breaks the work into reviewable slices, and lands them one at a time as small PRs into feature/<name>, so review happens continuously instead of at the end. Tracks the whole feature in one draft PR to master from the start. Also resumes an existing feature, reports its state, and marks that PR ready when the feature is done. Triggers: "bcp-feature X <prompt>", "start a feature branch for X", "continue the feature", "what's left on the feature", "land the feature".
---

# Running a Bernini feature branch

Small changes go straight to `master`. Anything large enough that master would be broken, half-done
or unreviewable in the middle of it gets an **integration branch**: `feature/<name>`. The work is cut
into slices, each slice becomes its own small PR into that branch, and the reviewer sees the feature
arrive continuously rather than as one wall of diff at the end. Only the finished feature is proposed
to `master`.

This skill runs that whole loop. It plans, implements and opens PRs; it never merges them — that
call stays with the user, at every step.

## Invocation

| | |
|---|---|
| `bcp-feature <name> <prompt>` | start or resume `feature/<name>`, plan the work, begin landing slices |
| `bcp-feature <name>` | resume: report state, then continue with the next slice |
| `bcp-feature` | status only, for the active feature |
| `bcp-feature --land` | rebase, verify the whole feature, mark its tracking PR ready |

## State

Two pieces, both local to this clone and neither ever committed:

```bash
git config --local bernini.feature feature/<name>   # which feature is active
git config --local bernini.feature                  # read it; unset means "landing on master"
git config --local --unset bernini.feature          # done with it
```

and the **plan file**, `.claude/features/<name>.md` (git-ignored), which is what makes the loop
resumable across sessions:

```markdown
# feature/culling — <one line: what the feature delivers>
base: master
tracking: #119

- [x] frustum planes on the camera — #112, merged
- [>] per-instance cull compute pass — #118, in review
- [ ] cull results feed the indirect draw
- [ ] editor toggle + stats readout
```

`[ ]` pending, `[>]` PR open, `[x]` merged. Rewrite it after every state change: it is the only
record of where the feature is, and a stale one sends the next session to re-do merged work.

## 1. Set up or resume the branch

```bash
export PATH="$PATH:/c/Program Files/GitHub CLI"   # gh is often not on PATH
git fetch origin
```

The tree must be clean first — a branch cut from a dirty tree drags unrelated work into every PR
based on it. Stash or commit before switching.

If `feature/<name>` already exists locally or on `origin`, this is a **resume**: check it out, fast
forward it, record it, read the plan file, and reconcile it with reality before doing anything else —
a slice marked `[>]` may have merged since the last session.

```bash
gh pr list --base feature/<name> --state all --json number,title,state
```

Otherwise cut the branch from `origin/master` and publish it, so PRs have something to target:

```bash
git switch -c feature/<name> origin/master
git push -u origin feature/<name>
git config --local bernini.feature feature/<name>
```

**Nothing is ever committed on `feature/<name>` itself.** It starts exactly equal to `master` and
only changes when a slice PR merges.

### The tracking PR

The feature also gets its own PR to `master`, opened **now** rather than at the end, as a **draft**:

```bash
gh pr create --base master --head feature/<name> --draft \
  --title "<what the feature delivers>" --body "..."
```

Empty at first — that is fine and it is the point. It accumulates every slice as they merge, so one
page shows the whole feature: the combined diff, the slice list, CI over the integration branch. The
alternative is a feature nobody can see until the last slice lands.

Draft matters. An ordinary PR sitting on a half-finished feature is one mis-click from merging a
partial port into `master`, and § 6 is what marks it ready.

**On resume, check it still exists and open it if not** — the feature may have been started before
this was the rule, or the PR may have been closed:

```bash
gh pr list --head feature/<name> --base master --state open --json number,isDraft
```

Record its number at the top of the plan file (`tracking: #NNN`) so the next session does not have to
go looking.

### Naming

| | |
|---|---|
| Feature branch | `feature/<name>` — `feature/culling`, `feature/webgpu-port` |
| Slice branch | `<type>/<name>-<slug>` — `feat/culling-frustum-pass`, `fix/culling-aabb-sign` |

Slice branches are **siblings** of the feature branch, not children. Git stores refs as paths, so
once `feature/culling` exists no ref may begin `feature/culling/` — the push fails with
`cannot lock ref ... exists; cannot create`, which does not explain itself.

## 2. Decompose the prompt into slices

Do this **after** reading the code, not from the prompt alone — read the docs the change touches
(the index is in [CLAUDE.md](CLAUDE.md)) and the real source first. A decomposition invented from
the prompt text splits along the words rather than the seams, and every slice then fights the last.

A slice is one PR. It must:

- **build and pass on its own.** Never "part 1 of 2, compiles after part 2". A reviewer bisects these.
- **be reviewable in one sitting.** If it cannot be described in one sentence without "and", it is two.
- **rest on what came before.** The natural order is bottom-up by layer — `bgl`, then `assetlib`,
  then `gamelib`, then `apps/editor` — because that is the direction the dependencies point. A
  refactor that enables the feature is its own slice, ahead of the feature.
- **be worth reviewing alone.** Do not split a 20-line change into three PRs to look incremental.

Dead scaffolding is the one thing that justifies a slice landing unused: a `bgl` slice can add an
interface nothing calls yet, provided the tests call it. Say so in the PR body.

Write the plan file, then **show the user the slice list in one short block and start on the first
one**. Do not wait for approval unless plan mode is on or the prompt asked for a plan — but do stop
and ask if the decomposition turned out to be a design question you cannot settle from the code.

The plan is a hypothesis. When a slice proves it wrong, rewrite the plan file and say what changed
and why — that is the loop working, not a failure.

## 3. Land one slice

Repeat this per slice. Only one is in flight at a time unless the user asks otherwise (§ 4).

```bash
git fetch origin
git switch -c <type>/<name>-<slug> origin/feature/<name>
```

Always from `origin/feature/<name>` — never the local branch (stale), never whatever is checked out
(drags in unrelated work).

Then follow [bcp-implement](.claude/skills/bcp-implement/SKILL.md) §1–§7 exactly as written: read
first, commit boundaries decided before coding, STYLE.md (comments are a last resort, a better name
beats a comment), tests that pin behaviour, build, **read the logs**, update the docs in the same
commit. Nothing in that loop changes here.

Two things apply only inside a feature:

- **Depend on what already landed on the feature branch, not on master.** The helper an earlier
  slice added is in `origin/feature/<name>`. Do not write a second one.
- **Hold the slice boundary.** Work that belongs to a later slice goes in that slice, even when it
  is three lines and right there. Scope creep is what turns the last PR into the wall of diff this
  branch exists to avoid.

Verify, format, then open the PR:

```bash
just build && just test                      # or the suites the change touches
just run bgl_tests -- --gpu-validation       # if it touches shaders, barriers or descriptors
just format <files...>
git fetch origin && git rebase origin/feature/<name>   # the base moved if a sibling merged
just build && just test                                 # again — a rebase is a real merge
git push -u origin HEAD
gh pr create --base feature/<name> --title "..." --body "..."
```

`--base` is not optional. Without it `gh` targets the repo's default branch and the PR silently
proposes the work to `master`.

The body says what changed, **why**, how it was verified (name the suites; say whether GPU validation
ran), and — because this is a partial change — which slice of the plan it is and what still has to
land. A reviewer must be able to tell a deliberate gap from an oversight.

Update the plan file with the PR number and `[>]`.

## 4. Stop for review

After the PR is open, **stop and report**: the PR, what it contains, what the next slice is. The user
reviews and merges. Review comments are [bcp-revise](.claude/skills/bcp-revise/SKILL.md), which
pushes to the slice branch and leaves the base alone.

When they merge, `bcp-feature <name>` picks up the next slice from the plan file.

If the user would rather not wait, the next slice **stacks** on the open one instead of on the
feature branch:

```bash
git switch -c <type>/<name>-<slug2> origin/<type>/<name>-<slug>
gh pr create --base <type>/<name>-<slug> --body "Stacked on #<n>."
```

GitHub re-targets a stacked PR onto the parent's base when the parent merges, but the branch still
needs `git rebase origin/feature/<name>` and a `--force-with-lease` push, or the PR shows the
parent's commits as if they were new. Only stack when asked: a stack that grows faster than the
reviewer merges it recreates the big-bang review one level down.

## 5. Keep the feature fresh

`master` moves. Rebase the feature branch onto it when it drifts, and only when **no slice PR is
open** — a rebase rewrites the commits those PRs are based on, and each one then shows a diff nobody
wrote:

```bash
git switch feature/<name> && git fetch origin && git rebase origin/master
git push --force-with-lease
```

With PRs open, merge them first, or merge `master` in instead of rebasing and say why.

## 6. Land the feature

When the plan file is all `[x]`, the tracking PR from § 1 becomes the real thing:

```bash
git fetch origin && git switch feature/<name> && git rebase origin/master
just build && just test                      # the whole feature at once
just run bgl_tests -- --gpu-validation       # if any slice touched shaders, barriers or descriptors
git push --force-with-lease
gh pr edit <tracking> --title "..." --body "..."
gh pr ready <tracking>
```

This full run matters more than any single slice's did: each slice was verified against the feature
branch as it stood at the time, and this is the first time all of them exist together.

Rewrite the body — it has been accumulating since the branch was cut, and what it says now is the
feature's story: what it adds, why, how it was verified as a whole, what was deliberately left out.
Link the slice PRs; do not restate them. After it merges:

```bash
git config --local --unset bernini.feature
git switch master && git pull && git branch -d feature/<name>
rm .claude/features/<name>.md
```

## Rules

- **Never merge a PR.** Not the slice PRs, not the tracking PR. Continuous review is the entire point
  of this branch, and it only works if a human is the one approving.
- **Never take the tracking PR out of draft** before § 6. It is open from the start precisely so the
  feature is visible while it is still unfinished.
- **Never commit directly onto `feature/<name>` or `master`.** Everything arrives by slice PR.
- **Never open a PR without `--base`.** The default branch is `master` and it will be wrong.
- **Never let a slice land broken.** "Fixed in the next PR" defeats bisecting and wastes the review.
- **Never claim a test passed without running it.** If a step was skipped, say so.
- **Keep the plan file true.** It is the only thing that survives the session.
- **Push back** — if the work does not need a feature branch, say so; one PR to `master` is cheaper.
  If a slice is too big to review, split it. If the prompt asks for something that breaks a layering
  rule or a documented invariant, argue rather than comply.
