---
name: bcp-revise
description: Use when acting on review feedback left on a Bernini pull request — fetch the review comments from GitHub, address each one in code, update docs, test, and push. Triggers: "address the PR comments", "handle the review feedback", "apply the review on #40", "respond to the review".
---

# Acting on a PR review

The job is to **answer every comment** — in code where the reviewer is right, and in words where
they are not. A review is a conversation, not a work order.

## 1. Fetch the comments

Three different things get called "comments" and they live in separate places. One command returns
all three:

```bash
just pr comments {n}
```

It prints the submitted `reviews`, the inline `threads` — each with the `id` to answer, its
`path:line`, and whether it has been `answered` — and the top-level `issue_comments`. The threads
matter most: they are anchored to code, and `answered: false` is the checklist for step 8.

Get the PR number from `gh pr view --json number` on the current branch if it was not given.

**Resolve threads only after the fix is pushed**, not before.

## 2. Read the code before agreeing

For each comment, open the file at that line and read the surrounding code. The reviewer may be
working from a stale diff, may have missed a constraint elsewhere, or may simply be right in a way
that is bigger than the line they flagged.

Do not fix a symptom the reviewer pointed at if the cause is one layer up. Say so, and fix the
cause.

## 3. Triage each comment

Sort every comment into exactly one of:

- **Accept** — the reviewer is right. Fix it.
- **Accept, wider** — right, and the same mistake exists elsewhere. Fix all of them and say you did.
- **Push back** — the change would be wrong, would break a documented invariant, or trades away
  something the reviewer may not have seen. **Do not silently comply.** Say why, name the constraint,
  and propose an alternative. A reviewer would rather be argued with than have their bad idea
  implemented.
- **Needs the author** — the comment is a design question you cannot settle from the code. Ask the
  user; do not guess.

Nothing is dropped. A comment with no reply reads as ignored.

## 4. Implement

Follow [STYLE.md](STYLE.md) and the layering rules in [CLAUDE.md](CLAUDE.md), same as
[bcp-implement](.claude/skills/bcp-implement/SKILL.md).

Reviewers frequently flag a name or a comment. Two reflexes:

- If the note is "what does this mean?" about an identifier, the answer is almost never a comment —
  it is a **better name**. Rename it and delete the comment that was propping it up.
- STYLE.md § Comments (CRITICAL) is the standard. Never add a comment that narrates, restates the
  code, or explains the diff.

If a review reveals the design is wrong, say so plainly rather than patching around it.

## 5. Update docs

A review comment often exposes a doc that is now false — a stated invariant the change broke, a
"this is the only place X happens" that is no longer true. Fix it in the same commit as the code.
Use [bcp-docs](.claude/skills/bcp-docs/SKILL.md) for `docs/` subsystem pages.

## 6. Verify

Rerun the suites the change touches — and if the review touched **shaders, barriers, or
descriptors**, rerun GPU validation, because a review-driven change to a barrier is exactly the kind
that silently corrupts:

```bash
just build
just test <suites...>
just run bgl_tests -- --gpu-validation   # only when the change warrants it
```

Read the logs (`build/<preset>/bin/*.log`), and check their timestamps — a stale log proves nothing
about this run. Never report green without having looked.

## 7. Commit and push

Format first (`just format <files...>`), then commit.

**Prefer new commits over amending.** A reviewer needs to see what changed *since* they looked; a
force-pushed rewrite destroys that. Only rebase if the branch has genuinely fallen behind, and say
so — onto the PR's **own base** (`gh pr view {n} --json baseRefName --jq .baseRefName`), which is a
`feature/...` branch for work inside a feature and not always `master`.

```bash
git push
```

## 8. Reply on the PR

Close the loop **on GitHub**, not just in chat. The reviewer should not have to diff your push
against their comments to find out what you did.

**Answer each comment where it was made.** A reply in the reviewer's thread sits under their
question; a summary at the bottom of the conversation makes them hunt for it and leaves the thread
open.

```bash
just pr reply {comment_id} --body-file <file>   # the id from step 1; goes into its thread
just pr comment {n} --body-file <file>          # one summary, after every thread has a reply
```

Both post as the **morgana-coding-agent bot**, so replies don't read as if the author wrote them by
hand — see [docs/ai-coding.md](docs/ai-coding.md). `gh pr comment` and the raw replies endpoint are
blocked; `just pr comment` refuses while any thread is unanswered, and `just pr check {n}` lists what
is outstanding.

Reply to **every** comment — including the ones you declined, with the reason. Then restart the
watcher (the turn cannot end otherwise) and summarise for the user: what you accepted, what you
pushed back on and why, and anything still awaiting their decision.

## Rules

- **Never claim a test passed without running it.**
- **Never silently ignore a comment.** Address it or argue with it.
- **Agreement is not the goal; a correct change is.** Complying with a wrong review comment because
  it is easier than arguing is the worst outcome here.
