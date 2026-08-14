# ai-dev-flow — implementation plan

## Context

Both workflow skills start at *read the docs* and go straight to design.
[bcp-feature § 2](../../.claude/skills/bcp-feature/SKILL.md) writes `docs/plans/<name>.md` from the
prompt text, and [bcp-implement § 2](../../.claude/skills/bcp-implement/SKILL.md) plans only when
plan mode asks it to. In both, the agent decides the seam, the module boundary and what "done" means
before anyone has agreed those are the right questions, and the user's first chance to disagree is a
finished document — where the only available move is filling in blanks in a blueprint already drawn.

The decisions that go wrong are made in the silence between the prompt and the first heading. This
change puts an interrogation there.

## Decisions

- **ADR-1 — The grill is its own skill, `bcp-grill`, invoked as § 0 by both workflows.** One place to
  tune the questions, and it stays invocable alone before the workflow is chosen. *Rejected: an
  inline § 0 in each skill, because two copies of the same rules drift; and grilling only in
  bcp-feature, because one-shot PRs are where an unexamined premise is cheapest to ship.*

- **ADR-2 — Brainstorming is a second skill, `bcp-brainstorm`, not a mode inside the grill.**
  Widening and narrowing want opposite rules — one forbids converging, the other exists to converge —
  and a single file holding both invites the agent to pick the wrong half. *Rejected: a brainstorm
  escape hatch inside bcp-grill; and grill-only, which leaves an undecided direction unowned.*

- **ADR-3 — Survey first, then grill.** "Should we reuse the existing interface?" is unanswerable
  from an agent that has not read the code — it is a question the agent must ask *concretely*, naming
  the interface and the line. The survey is bounded to `bcp-docmap`, the obvious headers and the
  roadmap's Guiding Constraints, and designing during it is forbidden, because once a solution exists
  every later question is measured against it. *Rejected: grilling cold, which produces questions the
  user cannot answer; and interleaving reads with questions, which is the most faithful to a real
  conversation but leaves no checkpoint a skill can enforce.*

- **ADR-4 — The consensus is the head of `docs/plans/<name>.md`: Context, Decisions, Non-goals,
  Acceptance.** One document, reviewed as one PR, with the agreed part above the derived part.
  *Rejected: a separate `docs/adr/<name>.md`, two files to keep in sync; and a git-ignored scratch
  note, which hides the boundaries from the reviewer and from the precheck.*

- **ADR-5 — bcp-implement writes a plan document too, as the PR's first commit, and keeps it.** A
  one-shot needs something committed for the reviewer to read ahead of the diff and for the precheck
  to measure the diff against; a PR body is neither. *Rejected: the PR body alone; and deleting the
  plan in a trailing commit of the same PR, which erases it from the very diff the reviewer and the
  precheck were meant to read it in.*

  It persists where a feature's plan does not, and the difference is the **content**, not the
  lifetime rule. `bcp-feature § 5` deletes a plan because it holds a survey, a what-changes and a
  task list — all mirrors of code, all of which go stale. What a one-shot keeps is the head alone: a
  decision and the alternative it rejected, recorded on a date. That cannot come to disagree with the
  code, because it never claimed to describe it. The disposal rule that replaces deletion is that an
  ADR is amended **only by a change that reverses it**, never to make it match code that drifted, and
  `docs/plans/` is added to CLAUDE.md's index saying so — otherwise it is an unindexed directory
  accumulating documents nothing maintains.

- **ADR-6 — Every invocation is grilled. No size threshold, no escape hatch.** A small change gets a
  small grill. *Rejected: letting the agent judge and declare its skips, because a skip an agent may
  declare is a skip an agent will take; and grilling features only.*

- **ADR-7 — With no human at the keyboard, the grill stops and waits.** An unattended `ws feature` or
  a `--continue` resume asks its questions, says it is blocked, and ends the turn. *Rejected:
  self-grilling with assumptions flagged "unconfirmed", which manufactures exactly the unread
  assumptions the skill exists to prevent; and skipping when non-interactive, which exempts the path
  that most needs the gate.* The cost is accepted knowingly: such a session idles until someone
  attaches.

- **ADR-8 — `bcp-precheck` gains a fourth question: does the diff cross a non-goal or contradict an
  ADR?** Boundaries nothing checks are boundaries that drift. It reports `revise`, not `block` — a
  boundary may move, but not silently. *Rejected: leaving the consensus advisory.*

## Non-goals

- **The `ws` scripts.** `ws feature <name> "<prompt>"` still collects one prompt up front, and the
  scripts live in the parent `bernini-workspace` repo, outside this checkout. Teaching the launcher
  that a fresh feature now blocks on a conversation is a separate change in a separate repo.
- **A `docs/` page describing the workflow.** The skills are the documentation; a second prose copy
  is a second source of truth.
- **`bcp-review`, `bcp-revise`, `bcp-docs`.** Untouched. The grill acts before code exists; those
  three act after it does.
- **Any programmatic enforcement of § 0.** No hook refuses a turn that skipped the grill, the way the
  `Stop` hook refuses an unwatched PR. The grill is a rule in a skill.

## Acceptance

There is no suite: this change is prompt text and adds no code. The gates that exist:

- Every cross-reference resolves — each linked path exists, and each `§ n` cited in one file names a
  section that is present in the target. Checked mechanically over `.claude/` and `docs/plans/`.
- `bcp-feature § 3`'s hand-off still reads **§1–§7** of bcp-implement, excluding the new § 0, and
  `bcp-implement § 8` / `bcp-feature § 4-5` still resolve after the precheck renumber.
- The first real invocation grills before it writes. This plan is that invocation's output.

## Commits

1. `docs(plans): plan the grill-first dev lifecycle` — this file.
2. `feat(skills): grill a request before anything is written` — `bcp-grill`, `bcp-brainstorm`.
   Gate: both load, and their links resolve.
3. `feat(skills): make the grill § 0 of both workflows` — `bcp-implement`, `bcp-feature`.
   Gate: the §1–§7 hand-off and the § 8 / § 4 references still resolve.
4. `feat(agents): read the diff against the boundaries the grill set` — `bcp-precheck` § 4 and the
   renumber. Gate: no dangling section reference into the agent file.
