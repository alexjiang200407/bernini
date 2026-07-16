---
name: bcp-review
description: Use when reviewing a Bernini pull request — read the diff, check it against the layering rules, the include conventions and STYLE.md, verify every finding against the code, then post the review on GitHub. Triggers: "review this PR", "review #40", "review the branch", a comment tagging the review bot.
---

# Reviewing a Bernini pull request

The job is to find **real defects and real convention breaks**, and to post nothing else. A review
that is mostly noise gets skimmed, and the one finding that mattered goes with it.

This repo's conventions are unusual enough that a generic reviewer produces confidently wrong
findings — asking for includes that are banned, asking for comments that are banned. § 4 is not a
footnote; it is half the job.

## 1. Get the diff

```bash
export PATH="$PATH:/c/Program Files/GitHub CLI"   # gh is often not on PATH

gh pr diff {n}                       # the change itself
gh pr view {n} --json title,body     # what the author says it does
```

Get the PR number from `gh pr view --json number` on the current branch if it was not given.

Read the author's description first. A review that flags something the description already explains
as deliberate is a review that did not read.

## 2. Read the code, not just the diff

A diff hides its own context. For every hunk that looks wrong, open the file and read around it —
the invariant that makes it correct is usually ten lines up, and the bug is usually the line the
diff did *not* touch.

Two things only exist outside the diff, and both are common here:

- **What the change broke elsewhere.** A renamed field, a changed lifetime, a new precondition.
  Grep for the callers.
- **What the change should have touched and didn't.** The second call site of the same pattern, the
  doc that now states something false.

## 3. What to look for

Roughly in order of how much damage it does.

**Layering** — the rule that is invisible in a diff and expensive to undo:

- `bgl` must never link or include `assetlib`. It stays codec-free and takes decoded
  `assetlib_structs` PODs.
- `assetlib` must never link or include `bgl`. The CLI baker must not drag in D3D12.
- `gamelib` is the only seam that links both.

A new `#include` that crosses one of these is a finding even when it compiles today.

**Correctness** — the ordinary review. Lifetimes, ownership, error paths, off-by-ones, resources
released on every path. For barriers, descriptors and shader bindings, be slower and more suspicious
than the diff size suggests: those fail silently and the tests may not catch them.

**Includes** — mechanical, and easy to get wrong:

- Paths resolve from the subsystem's `src/` and `include/` roots. `#include "X.h"`, never
  `#include "../X.h"`.
- `<>` for a header under `include/`, `""` for one under `src/`.

**Comments** — see § 4 first, then STYLE.md § Comments (CRITICAL). A comment that narrates, restates
the code, or explains the change rather than the code is a finding: it should be deleted, not
reworded.

**Docs** — CLAUDE.md's Documentation Index is a contract. If the change touches something a page in
`docs/` describes, that page changes in the same PR or the omission is a finding. The usual miss is
a stated invariant the change quietly falsified.

**Naming** — if you cannot tell what an identifier holds without reading its definition, the finding
is the name, not a missing comment.

## 4. What is not a finding

Do not post any of these. Each is a rule of this repo that a general-purpose reviewer reliably gets
backwards:

- **A missing standard library include.** Every target force-includes `PCH/pch.h`, which already has
  the standard library. Never ask for `#include <vector>`, `<string>`, `<memory>` and friends. Their
  *absence* is correct; their *presence* is the finding.
- **A missing comment.** The default is no comment. Asking for one is only ever right when the code
  cannot show a constraint — a non-obvious pre/post-condition, a hazard, or why the obvious approach
  was rejected. "Add a comment explaining this" is almost always the wrong review.
- **A CMakeLists that wasn't updated for a new source file.** Sources are globbed with
  `CONFIGURE_DEPENDS`. Adding a file where its siblings live is the whole procedure.
- **Formatting.** `just format` owns it. Never spend a comment on whitespace, brace placement or
  column width.
- **Stale golden images after a deliberate render change.** Regenerating goldens is the author's
  call, not a defect.

If you are about to write one of these, you have found a convention, not a bug.

## 5. Verify before posting

For each finding, before it goes in the review:

- **Point at the evidence.** Name the file and line you read that makes it true. A finding you
  cannot ground is a guess, and a guess costs the author more than silence does.
- **Try to refute it.** Ask what would have to be true for the code to be correct as written, then
  check whether it is. Most first-pass findings die here.
- **Check § 4 again.** It catches more than you expect.

Report coverage, not confidence-filtered highlights: every surviving finding goes in, with its
severity stated plainly, so the author can rank them. Do not silently drop one for being small —
say it is small.

## 6. Post the review

One review, findings anchored to lines. Not a wall of prose the author has to map back onto the
diff themselves.

```bash
gh api repos/{owner}/{repo}/pulls/{n}/reviews \
  -f event=COMMENT \
  -f body="<summary: what the change does, then what you found>" \
  -f 'comments[][path]=libs/bgl/src/...' \
  -f 'comments[][line]=42' \
  -f 'comments[][body]=<one finding>'
```

Use `event=COMMENT`. Do not `APPROVE` or `REQUEST_CHANGES` — that call is the author's.

The summary leads with the outcome: what the change does, then what you found, then the detail.
If you found nothing, say that in one line and stop. A clean review is a legitimate result and
padding it out to look thorough is worse than useless.

## Rules

- **Never invent a finding to have something to say.** Zero findings is an outcome, not a failure.
- **Never flag anything in § 4.** Those are conventions, and telling the author to break their own
  guide trains them to ignore the reviewer.
- **Never claim a test result you did not observe.** CI compiles this repo; it does not run the
  suites, and you have no GPU. Do not say a change passes or fails tests.
- **Ground every finding in a line you read.** If you cannot cite it, drop it.
