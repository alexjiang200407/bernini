---
name: bcp-cleanup
description: Use when clearing out the branches a finished PR left behind — classifies every local branch by its pull request's state and deletes only the ones GitHub says merged, keeping closed, open, unowned and worktree-held branches whatever git thinks. Triggers: "clean up the branches", "delete the merged branches", "my branch list is enormous", "tidy up the repo".
---

# Cleaning up branches

One rule: **a branch is deleted when its pull request merged, and never otherwise.**

## Why git cannot answer this

`git branch --merged` is the obvious tool and it is wrong here. This repository
squash-merges, so a merged branch's commits never become ancestors of `master` — on a
checkout carrying 93 landed branches, `--merged` reported *all* of them as unmerged. An
agent that trusts it deletes nothing; one that then reaches for `-D` to force past the
refusal deletes everything.

GitHub is the only thing that knows, so the predicate is `gh pr list --state all`.

## Run it

```bash
python scripts/cleanup_branches.py             # the listing; writes nothing
python scripts/cleanup_branches.py --delete    # cut them
```

It fetches with `--prune` first, classifies every local branch, and prints both halves —
what goes, and what stays with the reason it stayed. Read the *keep* list before passing
`--delete`; it is the half that catches a surprise.

`--remote` additionally pushes the deletes for merged branches `origin` still carries.
GitHub deletes a merged head branch itself, so this is normally a no-op — which is why it
is a separate flag: a `git push --delete` is outward-facing and irreversible from here.
Ask before passing it.

## What is kept, and why each one matters

| Kept | Because |
|---|---|
| **Closed, unmerged** | Its work is *by definition* not on `master`. This is the one branch here whose deletion loses something real. |
| **Open** | The PR is live. |
| **No pull request** | Either a feature's integration branch (`feat/<name>`, which task PRs target and which only `ws done` removes) or a local scratch ref whose contents nobody has vouched for. |
| **Checked out in a worktree** | A sibling session is working in it. Deleting it breaks that checkout, not this one. |
| **The current branch, `master`** | — |

The closed case is not theoretical. On this repo `skinned-mesh-prerebase` — no PR, so
kept — turned out to carry a `docs/specs/clip_loop_convention.md` and a `Clip.slang`
loop-convention fix that never landed. A cleanup that treated "looks finished" as
"merged" would have taken it.

## When the keep list is long

The script does not judge, so a long keep list is normal and is not a problem to solve.
Do not offer to delete a kept branch to shorten it. Two are worth *reporting* to the user
rather than acting on:

- A **closed** PR's branch — say which PR closed it, and let them decide whether the work
  is abandoned or waiting.
- A **no-PR** branch that is not one of the live `feat/<name>` integration branches — a
  scratch ref that has outlived its purpose. Say what is on it (`git log --oneline
  origin/master..<branch>`) so the user can answer; do not answer for them.

## Rules

- **Never `git branch -d`** expecting it to protect you, and never `-D` from judgement.
  The pull request state is the check; `-D` is only how the script executes a decision
  already made.
- **Never delete a branch on `origin` without being asked.** The local half is recoverable
  from the reflog for 90 days; the remote half is not recoverable from here at all.
- **Never widen the predicate.** "Its commits look like they landed", "the remote is gone",
  "it is behind master" are all things that are true of branches which still hold work.
  Merged, or kept.
