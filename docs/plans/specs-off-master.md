# specs-off-master — implementation plan

## Context

[#577](https://github.com/alexjiang200407/bernini/pull/577) put *draft* specs on an orphan branch and
left the landed ones in `docs/specs/` on master, split by whether a pull request had moved a file
across. The split was the wrong axis. A spec is an artefact of a decision in progress — it describes
code that does not exist — and `docs/` is the map of what does. Mixing the two is what makes a doc
untrustworthy, and it is why the rule every other page in the index obeys (*"if you modify something
a doc touches, modify the doc"*) has never been applicable to a spec.

So no spec belongs on master, landed or not, and the drafts/landed distinction it was sorted by has
nothing left on one side of it.

## Decisions

- **ADR-1 — `docs/specs/` leaves master entirely, and the directory in a checkout becomes the
  symlink.** The whole of it is the `spec-drafts` worktree; there is no `drafts/` level, no landed
  half, and no pull request that moves a file across. A spec is written, revised and deleted on the
  branch. *Reverses #577's ADR-3, which put the symlink at `docs/specs/drafts/` so that "landed specs
  and drafts read as one directory" — there are no landed specs to read alongside.*

- **ADR-2 — The eight references on master into `docs/specs/` are stripped, not repointed.** Each site
  already states the fact it needed; the link was a pointer to more. Where a fact only existed in the
  spec — the `.banim` deserialize cost `bcp-precheck` tabulates — it moves into the `docs/` page that
  owns the subject rather than being lost. *Rejected: repointing them at `git show spec-drafts:<name>`,
  because it is dead for anyone reading on GitHub and it puts the workspace's branch name inside
  bernini, which `scripts/_project` records bernini must not know.* *Rejected: pushing the branch so
  the links resolve, which reverses the local-only decision to save link text.*

- **ADR-3 — `ask_guard.py` goes back to one spec root.** With `docs/specs` itself the symlink, the
  second root #577 added has nothing to reach that the first does not; resolving one root covers both
  the linked case and the unlinked one. *Rejected: keeping both roots as harmless, because a rule with
  a branch nothing takes is a rule the next reader has to disprove.*

- **ADR-4 — An unlinked checkout still lets a spec be written, and `ws init` rescues it.** Where there
  is no symlink — CI, a fresh clone — `docs/specs/<name>.md` resolves inside the checkout and is
  allowed, exactly as before. It lands untracked, which is the old failure; the migrate pass is what
  collects it. *Rejected: refusing the write when the worktree is absent, because an ask session would
  then be able to write nothing at all, and a spec lost to a missing symlink is worse than one lost to
  a missing commit.*

## Non-goals

- **Moving `docs/plans/`.** An ADR records a change that happened and is written inside that change's
  branch, so it is documentation of the real tree and belongs on master.
- **Deciding what the six specs are worth.** They move as they are. Whether any is stale, wrong or
  finished is a question for whoever next reads one.
- **Pushing the branch.** Unchanged from #577: local only.

## Acceptance

- `scripts/tests/test_ask_guard.py` — a spec is writable through a `docs/specs` symlink resolving
  outside the checkout, still writable in a checkout that has none, and every path refused before is
  still refused.
- `scripts/tests/test_draft_commit.py` — the hook commits under the resolved `docs/specs`, by both the
  editing tools and the shell, and is inert without a link.
- `git ls-files docs/specs` on master is empty, and `git grep 'docs/specs/[a-z]'` finds no reference
  outside `docs/plans/` and the hooks that implement the mechanism.
- `ws doctor` green, with every checkout linked at the new path.

## Commits

1. `docs(plans): plan taking specs off master` — this file.
2. `docs: say what the specs said, instead of linking to them` — the eight references. Gate: the
   `git grep` above.
3. `refactor(specs): docs/specs leaves master for the spec-drafts branch` — the six files, and the
   index entry that listed them. Gate: `git ls-files docs/specs` is empty.
4. `refactor(hooks): docs/specs is the worktree, so one root again` — both hooks and their tests.
   Gate: `just test scripts`.
5. `docs(ask): a spec is written to docs/specs and never lands` — `bcp-ask` § 5, CLAUDE.md,
   `docs/ai-coding.md`. Gate: read.
