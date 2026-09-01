---
name: bcp-implement
description: Use when implementing a feature or change in Bernini end-to-end — from reading the docs, through logical commits and tests, to a pushed PR. Handles the whole loop: research, plan, implement in verifiable slices, test, update docs, format, commit, PR. Triggers: "implement X", "add feature Y", "build me a Z", or any change large enough to want its own branch and PR.
---

# Implementing a change in Bernini

The goal is a **reviewable PR**: a chain of commits that each build and pass, tests that pin the
behaviour, docs that still tell the truth, and no comments that a better name would have made
redundant.

Work in the order below. Do not skip to coding — § 0 routinely destroys the design and § 1 routinely
changes it.

This is for a change that lands on `master` as **one** PR. If it is too large for that — or if
`git config bernini.feature` is already set — use
[bcp-feature](.claude/skills/bcp-feature/SKILL.md), which splits the work into slices and lands each
as its own PR into a feature branch. Steps 1–7 below are the same either way; § 0 runs once per unit
of agreement — per PR here, per *feature* there, not per task — and only the branch cut from and the
PR base differ.

## 0. Grill first

**No work starts until the request has been grilled.** Run
[bcp-grill](.claude/skills/bcp-grill/SKILL.md) on the prompt: survey narrowly, interrogate the
intent, and close on a consensus once every question has its answer. Every invocation, including a
two-line fix — the grill scales down, it is never skipped, and with nobody at the keyboard it stops
and waits on its questions rather than answering itself.

It decides what this skill cannot: whether the change is wanted at all, what the standard solution
is and whether the architecture we have is the right shape for it, which layer owns it, what it is
explicitly *not* doing, and what proves it works. It is also where *this is too big for one PR*
surfaces, which sends the work to [bcp-feature](.claude/skills/bcp-feature/SKILL.md) before anything
is cut.

Once the grill closes, write the consensus straight into `docs/plans/<name>.md` as the document's
head — do not ask for a chat confirmation first. It is an Architecture Decision Record, one entry per
decision:

```markdown
# <name> — implementation plan

## Context
What breaks today, and why now.

## Decisions
- **ADR-1 — <the decision>.** <why>. *Rejected: <the alternative>, because <why not>.*

## Non-goals
- <the adjacent thing this is explicitly not doing>

## Acceptance
- <the suite, tag, golden image or assertion that proves it>
```

Decisions and boundaries only — **no implementation steps**. § 3 appends the commit slices below it,
and that is the whole document.

**`docs/plans/` is not on master.** It is a symlink onto the `artefacts` branch, beside `docs/specs/`,
and `.claude/hooks/draft_commit.py` commits the file as you write it. A plan is a document about the
change rather than documentation of the tree, addressed to whoever reviews it — so it never enters a
commit here and never appears in the diff.

The consensus therefore reaches the reviewer through the **PR body**: `## Design notes` (§ 10) carries
one line per ADR, with the alternative each rejected, and that is where a boundary the user disagrees
with comes back as a comment. [`bcp-precheck`](.claude/agents/bcp-precheck.md) § 4 reads the diff back
against the plan file, which it opens through the symlink like any other file.

Keep it thin: anything describing how the code now **behaves** moves into `docs/` under § 7, and what
stays is the decision and the alternative it rejected. **An ADR is amended only by a change that
reverses it.** Never edit one to match code that drifted — that is precisely what turns it into a
second source of truth.

## 1. Read before writing

Read the docs that touch the area **before** designing anything. The index is in
[CLAUDE.md](CLAUDE.md); the ones that matter most often:

| Change touches | Read |
|---|---|
| renderer, passes, barriers | [docs/rhi.md](docs/rhi.md), [docs/framegraph.md](docs/framegraph.md), [docs/passes.md](docs/passes.md) |
| GPU-bound structs / descriptors | [docs/geometry_layout.md](docs/geometry_layout.md), [docs/idlgen.md](docs/idlgen.md) |
| assets, cooking, textures | [docs/asset_standards.md](docs/asset_standards.md) |
| debugging a GPU problem | [docs/gfx_debug.md](docs/gfx_debug.md) |

When the area spans more than one doc, spawn [`bcp-docmap`](.claude/agents/bcp-docmap.md) with the
Agent tool, `subagent_type: bcp-docmap`, one tier below your own model, and ask it the question you
actually have. It starts at the
index, follows the links the docs carry into further docs and into the headers they defer to, and
reports the answer with the line behind each claim — so the survey costs a paragraph of context
instead of a subsystem's worth. It reads only.

Then read the **real source**, not just the docs. Also read the nearest `CLAUDE.md` — each subsystem
has one (`libs/bgl_extended/`, `libs/gamelib/`, `apps/editor/`) and it holds rules the root one does not.

**Respect the layering.** `bgl_extended` never links `assetlib`; `assetlib` never links `bgl_extended`; `gamelib` is
the seam that links both and is where "load this asset into a scene" lives. If a change seems to
need a layering violation, the design is wrong — find the seam instead. Before writing a helper,
check whether one exists: duplicating a rule that lives in one place is how two code paths start
disagreeing.

## 2. Plan, if asked to

If plan mode is on, or the prompt asks for a plan, produce the plan and stop. Say what you will
change, per file, and what could break. § 0's ADRs already hold the decisions that were *agreed*;
what belongs here is the working-out below them — the trade-offs the grill did not reach, each with
the alternative rejected. A decision that crosses a stated non-goal is not a trade-off to record, it
is a reason to reopen the grill.

## 3. Slice into verifiable commits

Decide the commit boundaries **before** writing code. Each commit must build and pass on its own —
they are the units a reviewer bisects with.

The natural seam is the **layer**: one commit per `bgl_extended` / `assetlib` / `gamelib` / `apps/editor`
slice, bottom-up, so each rests on the one below. A refactor that enables the feature is its own
commit, ahead of the feature.

Append them to `docs/plans/<name>.md` under the § 0 head, each with the gate that proves it — the
suite, the tag, the golden image, the assertion. "It builds" is not a gate.

```markdown
## Commits
1. `refactor(bgl): …` — <what it moves>. Gate: `just test bgl_extended`.
2. `feat(gamelib): …` — <what it adds>. Gate: `just run gamelib_tests -- "[vat]"`.
```

## 4. Implement

Follow [STYLE.md](STYLE.md). Two rules earn their own mention because they are the ones most often
broken:

**Comments are a last resort.** See STYLE.md § Comments (CRITICAL). The default is *no comment*.
Write one only for a constraint the code cannot show: a hazard, a non-obvious pre/post-condition, or
why the obvious approach was *not* taken. Never narrate, never restate the code, never explain the
diff ("this is now per-instance", "moved from Submesh") — the commit message is for that, and such a
comment is dead weight the moment the PR merges.

**Reach for a better identifier first.** Most comments are a naming failure. If it needs a paragraph, it belongs in `docs/`, not the source.

Do not `#include` standard library headers — they are in the precompiled header. Include shared
headers with `<>`, subsystem-internal ones with `""`. New source files need no CMake edit; the globs
are `CONFIGURE_DEPENDS`.

## 5. Write tests

Every suite is Catch2, and a new `*_tests` executable target is discovered automatically.

Name a case for the **behaviour it pins**, not the function it calls — "The sink cannot be deleted"
— because the failure line is the bug report. Tag every case so it can be run alone.

Test the thing that would actually break. A test that cannot fail is worse than no test: it buys
false confidence. Prefer a test that proves a *negative* — the strongest test in this repo asserts a
prefetched texture uploads even though its file does not exist on disk, which is the only way to
prove the file was never read.

Where a GPU is needed, `bgl_extended_tests` renders headlessly and compares golden images; `editor_tests` can
create a device too (it links `bgl_d3d12_agility`), so anything that does not need a *window* is
testable. Tag GPU cases `[render]`.

## 6. Build and verify

```bash
just build                 # all targets
just test                  # every suite
just test editor gamelib   # only these
```

**Never run two builds against the build dir at once** — they fight over the same `.obj` files and
fail with "Permission denied". Run them sequentially.

Then **check the logs**, which is where problems appear that no assertion catches. They sit beside
the executable (`build/<preset>/bin/`): `bgl.log`, and a `<exe>_crash_<stamp>.log` if something died.
Crash logs are stamped and accumulate, so the newest is the only one this run wrote — check the
timestamps before drawing a conclusion from any log.

If the change touches **shaders, barriers, or descriptors**, run GPU-based validation before the PR.
It patches every shader, so it roughly doubles the suite — but it is the only thing that catches a
bad barrier:

```bash
just run bgl_extended_tests -- --gpu-validation
```

A run is not "passing" until you have read the log. Report failures with their output; never claim
green without having looked.

## 7. Update the docs

If the change alters something a doc describes, fix the doc **in the same commit**. A `CLAUDE.md`
that lies is worse than one that is missing, because it is trusted.

Check specifically: did this make a stated constraint false? ("None of them are covered", "the suite
takes about a second", "X is the only place that branch lives"). Those sentences rot silently. Use
[bcp-docs](.claude/skills/bcp-docs/SKILL.md) for `docs/` subsystem pages.

## 8. The critical read

**Every pull request is read by [`bcp-precheck`](.claude/agents/bcp-precheck.md) before it opens.**
It reads the diff against the base, so it runs *inside* § 9's sequence — after the format, the commit
and the rebase, and before the push. This section is what it does; § 9's block is where it goes.

Spawn it with the Agent tool, `subagent_type: bcp-precheck`, one tier below your own model:

> Review the diff against the base. Be as critical as the evidence allows.

It answers the questions the author is worst placed to answer about their own diff: has this code
already been written in `core`, does the design fight `ROADMAP.md` or depart from the standard with
no ADR saying so, does it cross a non-goal or contradict an ADR agreed in § 0's grill, is it feasible
at AAA asset scale, and does it break `STYLE.md`. It reports back; it posts nothing and edits nothing.

Act on its verdict before pushing:

- `block` — fix it, then run the precheck again. The PR does not open on a blocking finding.
- `revise` — fix what is right, and for anything you reject, say why in the PR body. A finding you
  disagreed with is worth a line so the reviewer does not re-derive it.
- `clean` — push.

Fixes go into the commits they belong in, not a "self-review" commit on the end. The cloud reviewer
(`/review` on the PR) is deliberately **not** automatic — this gate is what makes that affordable, so
a trivial PR never needs it.

## 9. Format, commit, PR

**Format before every commit**, or CI-visible noise lands in the diff:

```bash
just format <files...>          # in place
just format --check <files...>  # verify only
```

`docs/plans/<name>.md` is committed for you, on the artefacts branch, and is not part of this PR.

Commit each slice with a message that says **why**, not what — the diff already says what. Subject
line `type(scope): imperative summary`. Attribution is not your job: `.githooks/prepare-commit-msg`
co-authors every commit made from a Claude session to the morgana-coding-agent bot, so write no
`Co-authored-by` line yourself and never pass `--no-verify`.

Then rebase onto `origin/master` (it moves), rebuild, re-test, and open the PR. Write the body to a
file first, headed by `# type(scope): the title` — the title is lifted from that line, because `just`
joins a recipe's arguments on spaces and a quoted one would not survive:

```bash
git fetch origin && git rebase origin/master
# spawn bcp-precheck here (§ 8), and act on it before pushing
git push -u origin HEAD
just pr create --base master --body-file <file>
```

`just pr create` opens the PR as **you**: GitHub takes a squash-merged commit's author from the
PR's author, so a bot-authored PR would sign every line of `master` as the bot's. Comments are the
bot's; the pull request is yours. Raw `gh pr create` is blocked. It prints the PR number and arms
the watch — go straight to
[bcp-feature § 4](.claude/skills/bcp-feature/SKILL.md) and start `just watch-pr <n>` as the last
action of the turn. The turn cannot end until you do.

## 10. The body

It has a fixed shape, and a **budget of about 350 words**. A body over it is one where an
explanation of the code has escaped into the pull request — that belongs in `docs/`, in the commit
message, or nowhere. Prose is what a reviewer skims; the shape below is what they read.

```markdown
# type(scope): the title, lifted from this line

<what changed and why — three sentences>

## Design notes
- **<the decision>** — <why>. *Rejected: <the alternative>, because <why not>.*

## Verification
<the suites and their result, whether GPU validation ran, the preset built — a line or two>

## Left out
- <the adjacent thing this deliberately does not do, one line — omit the section when there is none>

## Needs a human
- [ ] **Windows** — <what to run there, and why CI cannot>
- [ ] **Eyes** — <what to look at, and what right looks like>
```

Nothing else. No *what was wrong* narrative, no *how it works now*, no note on scope: the diff, the
commits and `docs/` each already carry one of those. `## Design notes` is where § 0's ADRs land, one
line apiece, and `## Left out` is where its non-goals do — a reviewer must be able to tell a
deliberate gap from an oversight, and that is the only sentence in the body they cannot get from the
code. The plan file is not on master, so there is nothing to link: the two sections above are the
whole of what a reviewer sees of it.

**Do not tabulate the diff.** `just pr create` appends its own breakdown — production, shaders,
tests, docs, tooling, assets — computed from `git diff --numstat` against the base, inside a fenced
block that every `just pr edit` rewrites. A hand-written table is wrong by the first revision, and
this one is overwritten anyway.

### What earns a box

An unchecked box means **someone must act**. Never pre-tick one; when nothing is needed, say so as a
stated negative with its reason, so silence is never mistaken for an oversight:

```markdown
## Needs a human
- [ ] **Eyes** — the readout names the selected tab, and blanks when no viewport is rendering.

Windows: not needed — no D3D12, shader or platform code; CI covers the compile.
```

**Windows.** `.github/workflows/ci.yml` compiles on `windows-latest` *and* `macos-latest`, runs **no
suite on either**, and builds `apps/editor` on neither (Qt is absent). So a Windows compile failure
is caught for free and never needs a box; a Windows *behaviour* difference is caught by nothing.
Give it a box when you built on macOS and the change reaches:

- D3D12 in `libs/bgl_extended` — the RHI, barriers, descriptors, PSOs, the Agility SDK.
- shaders — DXIL is not the Metal path, and no runner compares a `[render]` golden image.
- paths and files — `libs/core/file`, separators, case, the mount-key rule in [STYLE.md](STYLE.md).
- `apps/editor` — nothing builds it in CI at all.

Name the command, not the need: *"`just run bgl_extended_tests -- "[taa]" --gpu-validation` on Windows"*.

Under [bcp-feature](.claude/skills/bcp-feature/SKILL.md) the box waits: a task PR merges into the
feature branch rather than into `master`, so the whole feature earns one box on the landing PR
instead of one per task. That skill's § 3 and § 5 own the rule.

**Eyes.** A box for anything whose result is a picture or a gesture, which no assertion reaches:
editor UI — a layout, a widget, a menu, what a panel reads; anything on screen — a pass's output, a
material, a light, a colour; anything about feel — the frame loop, a loading screen, an interaction.
This checkout cannot capture the screen, so an editor change leaves at least one. Say what *right*
looks like, so ticking it is a decision rather than a guess.

## Rules

- **Never start without the grill.** § 0 has no size threshold and no escape hatch, and a question
  the user never answered is not one that closed.
- **Never claim a test passed without running it.** If a step was skipped, say so.
- **Do not commit unprompted.** The user reviews the diff first unless they asked for the PR.
- **Push back.** If the request is wrong, or a shortcut would break a layering rule or a documented
  invariant, say so rather than quietly complying.
