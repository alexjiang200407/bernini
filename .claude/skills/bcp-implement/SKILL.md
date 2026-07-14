---
name: bcp-implement
description: Use when implementing a feature or change in Bernini end-to-end — from reading the docs, through logical commits and tests, to a pushed PR. Handles the whole loop: research, plan, implement in verifiable slices, test, update docs, format, commit, PR. Triggers: "implement X", "add feature Y", "build me a Z", or any change large enough to want its own branch and PR.
---

# Implementing a change in Bernini

The goal is a **reviewable PR**: a chain of commits that each build and pass, tests that pin the
behaviour, docs that still tell the truth, and no comments that a better name would have made
redundant.

Work in the order below. Do not skip to coding — step 1 routinely changes the design.

## 1. Read before writing

Read the docs that touch the area **before** designing anything. The index is in
[CLAUDE.md](CLAUDE.md); the ones that matter most often:

| Change touches | Read |
|---|---|
| renderer, passes, barriers | [docs/rhi.md](docs/rhi.md), [docs/framegraph.md](docs/framegraph.md), [docs/passes.md](docs/passes.md) |
| GPU-bound structs / descriptors | [docs/geometry_layout.md](docs/geometry_layout.md), [docs/idlgen.md](docs/idlgen.md) |
| assets, cooking, textures | [docs/asset_standards.md](docs/asset_standards.md) |
| debugging a GPU problem | [docs/gfx_debug.md](docs/gfx_debug.md) |

Then read the **real source**, not just the docs. Also read the nearest `CLAUDE.md` — each subsystem
has one (`libs/bgl/`, `libs/gamelib/`, `apps/editor/`) and it holds rules the root one does not.

**Respect the layering.** `bgl` never links `assetlib`; `assetlib` never links `bgl`; `gamelib` is
the seam that links both and is where "load this asset into a scene" lives. If a change seems to
need a layering violation, the design is wrong — find the seam instead. Before writing a helper,
check whether one exists: duplicating a rule that lives in one place is how two code paths start
disagreeing.

## 2. Plan, if asked to

If plan mode is on, or the prompt asks for a plan, produce the plan and stop. Say what you will
change, per file, and what could break. Name the trade-off you chose and the one you rejected.

## 3. Slice into verifiable commits

Decide the commit boundaries **before** writing code. Each commit must build and pass on its own —
they are the units a reviewer bisects with.

The natural seam is the **layer**: one commit per `bgl` / `assetlib` / `gamelib` / `apps/editor`
slice, bottom-up, so each rests on the one below. A refactor that enables the feature is its own
commit, ahead of the feature.

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

Where a GPU is needed, `bgl_tests` renders headlessly and compares golden images; `editor_tests` can
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
the executable (`build/<preset>/bin/`): `bgl.log`, and a `<exe>_crash.log` if something died. Note a
stale log from an earlier day is not evidence about this run — check the timestamps before drawing a
conclusion from one.

If the change touches **shaders, barriers, or descriptors**, run GPU-based validation before the PR.
It patches every shader, so it roughly doubles the suite — but it is the only thing that catches a
bad barrier:

```bash
just run bgl_tests -- --gpu-validation
```

A run is not "passing" until you have read the log. Report failures with their output; never claim
green without having looked.

## 7. Update the docs

If the change alters something a doc describes, fix the doc **in the same commit**. A `CLAUDE.md`
that lies is worse than one that is missing, because it is trusted.

Check specifically: did this make a stated constraint false? ("None of them are covered", "the suite
takes about a second", "X is the only place that branch lives"). Those sentences rot silently. Use
[bcp-docs](.claude/skills/bcp-docs/SKILL.md) for `docs/` subsystem pages.

## 8. Format, commit, PR

**Format before every commit**, or CI-visible noise lands in the diff:

```bash
just format <files...>          # in place
just format --check <files...>  # verify only
```

Commit each slice with a message that says **why**, not what — the diff already says what. Subject
line `type(scope): imperative summary`. End every commit message with:

```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

Then rebase onto `origin/master` (it moves), rebuild, re-test, and open the PR:

```bash
git fetch origin && git rebase origin/master
gh pr create --base master --title "..." --body "..."
```

`gh` may not be on `PATH`; it installs to `/c/Program Files/GitHub CLI`. Prefix with
`export PATH="$PATH:/c/Program Files/GitHub CLI"` if `gh` is not found.

The PR body should say what changed, **why**, how it was verified (name the suites, and say if GPU
validation ran), and what was deliberately left out. Known follow-ups belong there too — a reviewer
should not have to discover them.

## Rules

- **Never claim a test passed without running it.** If a step was skipped, say so.
- **Do not commit unprompted.** The user reviews the diff first unless they asked for the PR.
- **Push back.** If the request is wrong, or a shortcut would break a layering rule or a documented
  invariant, say so rather than quietly complying.
