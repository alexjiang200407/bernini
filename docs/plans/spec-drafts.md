# spec-drafts — implementation plan

## Context

[bcp-ask](../../.claude/skills/bcp-ask/SKILL.md) § 5 leaves a spec **untracked**, so that landing one
is a pull request somebody opens on purpose. The landing policy is sound; the implementation was
*"do not commit it anywhere"*, which buys two problems that have nothing to do with landing:

- An untracked file lives in exactly one working directory. `ws ask` runs in the main clone, so a
  spec it writes cannot be read from any `bernini.features/<name>/` checkout — the one place the
  feature that would *implement* it is being built.
- Nothing in git knows the file existed: no reflog, no `restore`, no recovery of any kind.

Both fired on 2026-09-01. `docs/specs/second_renderer.md` was deleted during a `git pull` and was
rewritten only because its content happened to still be in a live agent conversation.

## Decisions

- **ADR-1 — An orphan branch, `spec-drafts`, in bernini's own repository.** No shared ancestry with
  master, so it carries no bernini sources: a spec is validated against master as it is now, never
  against a snapshot travelling beside it. It is the standard git idiom for a payload that shares a
  repository but not a history — `git.git` carries `todo`, `man` and `html` this way, and GitHub
  Pages' `gh-pages` is the same shape. *Rejected: a branch cut from master, because #576 moved every
  path `second_renderer.md` cites, and a stale tree travelling with the drafts invites a reader to
  resolve `file:line` against the wrong code.* *Rejected: a separate repository, because it severs
  the shared object store, which is the property that makes everything below cheap.*

- **ADR-2 — The branch is local and never pushed.** Every feature worktree shares one object store,
  so a local commit already fixes both problems above. *Rejected: pushing for offsite backup,
  because it publishes half-formed thinking to `origin`, and the requirement was recoverability, not
  durability across machines.*

- **ADR-3 — Worktree'd once at the workspace root, symlinked into every checkout as
  `docs/specs/drafts/`.** This is exactly `test-project`'s shape (`scripts/_project`): one worktree
  outside every checkout, a relative symlink in, and a `/docs/specs/drafts` line in the *common*
  `.git/info/exclude` so one entry covers every worktree and bernini's tracked `.gitignore` stays
  checkout-agnostic. Checking it out once is also what sidesteps *"a branch checked out in a sibling
  worktree cannot be checked out in yours"*. *Rejected: a per-checkout worktree, because git permits
  only one.*

- **ADR-4 — The harness commits, not the model.** An ask session has no shell by design and that is
  not reopened: a `PostToolUse` hook on writes under the drafts path commits them. *Rejected:
  widening `ask_guard.py` to permit a narrow `git commit`, because every argument in that guard's
  own comment applies — a composed line hides the write, an allowlist of commands still owes an
  answer for every flag.* *Rejected: a rule saying somebody should commit afterwards, because that
  leaves open exactly the window the 2026-09-01 deletion happened in.*

- **ADR-5 — The commit hook fires for every session, not only `ws ask`.** A feature agent that
  revises a draft has the same loss window as an ask session. Unlike `ask_guard`, this hook is not a
  guard rail — committing somebody's draft costs them nothing. *Rejected: gating on `WS_ASK` to
  mirror `ask_guard`, because a draft edited from a feature checkout would go back to being
  uncommitted, which is the state this exists to end.*

- **ADR-5a — And on the shell, which is what a feature agent has and an ask session does not.** A
  `sed -i` or a heredoc writes a draft naming no file in the tool input, so matching only the editing
  tools would have left exactly the session ADR-5 names writing drafts nothing commits. The shell's
  path therefore ignores the tool input and looks at the worktree, gated on a read-only `status` so
  the commands that touch no draft — nearly all of them — pay one cheap call. *Rejected: parsing the
  command line for the path it wrote, which is the approach `ask_guard.py` records nine rounds of
  review abandoning.* *Rejected: narrowing ADR-5 to say shell edits are not covered, because the
  agent that implements a draft is the one most likely to revise it.*

- **ADR-6 — bernini finds the drafts worktree by resolving its own symlink, never by knowing the
  workspace layout.** `scripts/_project` records the rule: bernini stays checkout-agnostic, knowing
  nothing about a workspace that puts things beside it. Both hooks therefore resolve
  `<checkout>/docs/specs/drafts` and are inert when it is absent — which is every CI runner, every
  fresh clone, and every checkout whose `git clean -xfd` took the symlink. *Rejected: a configured
  or hardcoded path to `bernini-workspace/spec-drafts/`, because it makes bernini depend on a
  workspace it must not know about.*

- **ADR-7 — `ws init` migrates the untracked specs it finds.** Any untracked `docs/specs/*.md` in a
  checkout is moved onto the drafts branch and the loose copy removed, printing each one. It is the
  only step that rescues what is at risk today. *Rejected: reporting them from `ws doctor` and
  waiting for a person, because the two drafts standing right now are one `rm` from gone and the
  failure mode is that nobody notices.*

## Non-goals

- **ADR drafts (`docs/plans/`).** Same shape of problem, but an ADR is written inside its change's
  own branch and lands in that change's PR, so it is already tracked.
- **Keeping a spec's citations valid as master moves.** #576 invalidated a spec's paths wholesale
  and nothing here prevents that; a stale citation is still found by reading.
- **Changing what it takes to land a draft.** A draft becomes a spec by a pull request moving the
  file into `docs/specs/` on master. The orphan branch is never merged, rebased or reconciled.

## Acceptance

- `scripts/tests/test_draft_commit.py` — the new hook commits a write under a resolved drafts
  symlink, is inert when the symlink is absent, is inert for a path outside it, and reports rather
  than swallows a failed commit. Run by `just test scripts`.
- `scripts/tests/test_ask_guard.py` — an ask session may write `docs/specs/drafts/<name>.md` through
  a symlink that resolves outside the checkout, and every path the guard refused before is still
  refused.
- `ws doctor` reports the branch, the worktree and each checkout's symlink, and exits non-zero when
  one is missing.

## Commits

This PR is bernini's half. The workspace half — `scripts/_drafts`, and the `ws init` / `ws feature` /
`ws doctor` calls into it — lands as commits in `bernini-workspace`, which has no PR flow of its own.
Neither half does anything alone: the hooks below are inert until a checkout has the symlink, and the
symlink is useless until the hooks admit it.

1. `docs(plans): plan the spec drafts branch` — this file. Gate: read by the reviewer first.
2. `fix(hooks): let an ask session write a drafts directory that resolves outside the checkout` —
   `ask_guard.py` matches a second spec root. Gate: `just test scripts`.
3. `feat(hooks): commit every write under docs/specs/drafts` — the new `draft_commit.py`, its
   `PostToolUse` registration and the hook's entry in `docs/ai-coding.md`. Gate: `just test scripts`.
4. `docs(ask): a draft is written under docs/specs/drafts and committed for you` — `bcp-ask` § 5 and
   CLAUDE.md § Specs, which describe the workflow both hooks serve. Gate: read.
