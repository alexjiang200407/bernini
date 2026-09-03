---
name: bcp-spec
description: Use when the output is a spec — one file describing a problem we have deferred or a feature we intend to build, with the evidence behind it, what a shipping engine does, the solutions considered, and the trigger that makes it urgent. Writes no plan, no task list and no code. Triggers: "write a spec for X", "bcp-spec", "record this as a spec", "we're not doing this now but write it down", and bcp-ask § 5 when a question's answer turns out to be a problem worth keeping.
---

# Writing a spec

A spec describes **code that does not exist**. That is the whole of what makes it a spec rather than
documentation, and it is why `docs/` cannot hold one: every page there says what the tree *is*, and
the rule that keeps them worth reading — change the code, change the doc — has nothing to say about a
file describing code nobody has written.

Two kinds qualify, and they are the same document:

| | |
|---|---|
| **a problem we deferred** | `animation_compression.md` — measured, understood, not worth doing yet |
| **a feature we intend to build** | `second_renderer.md`, `mvp_game_runtime.md` — the shape agreed before anyone cuts a branch |

What they share is that nothing has been decided about *doing* it. The moment it is agreed and
scheduled, the artefact is a plan, not a spec.

## A spec is not a plan

The single most common failure is writing the plan and calling it a spec. They are different files
in different directories with different lifetimes, and confusing them costs twice: a spec full of
commit slices is unreadable a month later when the slices are wrong, and a plan that re-argues the
problem buries the decision the reviewer came for.

| | **spec** — `docs/specs/` | **plan** — `docs/plans/` |
|---|---|---|
| answers | what is wrong, and what could be done about it | what we are doing, and in what order |
| written | before anyone has agreed to the work | once [bcp-grill](.claude/skills/bcp-grill/SKILL.md) closes |
| named for | the problem | the change |
| carries | evidence, alternatives, a trigger | ADRs, non-goals, commit slices, gates |
| amended | freely, whenever the evidence moves | only by a change that *reverses* it |
| ends | deleted when the thing lands | kept |

So: **no implementation detail, no task breakdown, no commit slices, no gates, no estimates.** If
you find yourself writing "first change the IDL, then the shader", stop — that is
[bcp-implement § 3](.claude/skills/bcp-implement/SKILL.md)'s, and it does not exist yet because the
work has not been agreed.

## 1. Survey before writing

A spec written from memory is worse than none: it is a confident claim about code that has moved,
filed where the next reader will trust it.

Read the source. Start at the Documentation Index in [CLAUDE.md](CLAUDE.md); when the problem spans
more than one doc, spawn [`bcp-docmap`](.claude/agents/bcp-docmap.md) with the Agent tool,
`subagent_type: bcp-docmap`, one tier below your own model. Read `docs/plans/` for whether the shape
you are about to call accidental was in fact decided, and read the specs already there — the problem
is often a face of one somebody has already written up.

**Assume no shell.** A spec is most often written from an `ws ask` session, where
`.claude/hooks/ask_guard.py` refuses Bash outright rather than inspecting it. Read, Grep and Glob are
the toolkit, and they are enough. When the spec genuinely turns on history — when a decision was
made, what a commit measured — say so in the file and name what would have to be looked up.

## 2. What goes in one

Seven things. Not seven headings — a small spec merges them into four — but every one of them is a
question the file must answer, and the corpus on `artefacts` converged on all seven without anyone
writing them down.

### The title is the claim, not the topic

`# One record is doing the geom's job and the instance's`, not `# GPU scene instance split`. The
filename is already the topic; the heading is the sentence that tells a reader scanning eleven of
these which one is theirs. Seven of the eleven do this. The other four are noun labels
(`# Animation compression`, `# A second renderer for mobile and web`) and each one has to be opened
before you know whether it is the file you wanted.

### What it is — with the evidence attached

The problem, or the feature-shaped absence, stated once and then shown. **Evidence is a `file:line`
or a number, never an adjective.** "The decode is slow" is a feeling; "`DecodeVertex` loops over
`attributeCount` and switches on `semantic` and on `format` — per vertex, in the mesh shader, for a
rule that is constant for the whole submesh" is a claim someone can check and overturn.

Prefer the number where one exists, and prefer the *exact* number: `animation_compression.md` opens
with 59.7 MB, "which is the file exactly". Where nothing has been measured, **say that** — "no
timestamp query exists to price it" is a real finding and it is usually the first thing the trigger
depends on.

### What it costs, and what it will cost

Why this is on a list at all. Two shapes, and one of them is nearly always available:

- **The cost already being paid.** `pso_sort_key.md` — one axis of the key is reconstituted by hand
  in C++ and again in Slang, and a new blended PSO added to one list and not the other compiles
  either way.
- **The cost that multiplies.** The same spec's table: each roadmap axis is a *multiplier* on the
  enum, not an addition, and the kernels are sized by it.

If neither can be written, the spec probably should not exist. A problem with no cost is an
observation.

### What the standard is

Same rule as [bcp-grill § 2](.claude/skills/bcp-grill/SKILL.md): **the engine and the mechanism,
named**, with our code out of frame. `vertex_layout_per_submesh.md` names Unreal's vertex factories,
Unity's fixed stream set and Godot's format bitmask, then the shape common to all three — and that
shape is what its design is built on. `animation_compression.md` names ACL, and that it has been
Unreal's default codec since 4.25, which is why quantization outranks the thing that was actually
tried.

Where you cannot name the engine and the mechanism, **say you do not know**. That is not a reason to
drop the heading — a problem no shipping engine has a word for is the strongest possible reason to
be careful, and a spec that quietly implies a consensus is how a deviation nobody named gets built.

### The solutions — as settled as the evidence actually is

This is the section the user comes for, and it has three legitimate postures. Pick the one the
evidence supports, and say which it is:

| | |
|---|---|
| **settled** | `skeleton_append.md` — "The design settled on: remap by name at load" |
| **settled, with the trap named** | `gltf_occlusion.md` — "The trap that would make this worse than doing nothing" |
| **open** | `mvp_game_runtime.md` — "What is deliberately left undecided", and what would decide it |

Whichever it is, each option carries **what it costs** and **what it forecloses**, and a rejected one
carries **why**. Two are worth including and are usually missing —
[bcp-brainstorm](.claude/skills/bcp-brainstorm/SKILL.md)'s pair: *do nothing*, priced honestly, and
*bend something that already exists*.

The most valuable thing this section can hold is **what was tried and dropped**, with its
measurements. `animation_compression.md` is mostly that: constant-track collapse was written, tested
and abandoned on a table of numbers, and two traps the measurement exposed are written down so the
next attempt does not re-run them. Nobody can re-derive a negative result; everything else in a spec
can be.

**Ordering constraints belong here; ordering *plans* do not.** "Do not attempt (2) before the set is
actually closed, because closing it is a cook-side decision" is a property of the problem. "Commit 1
changes the IDL" is a plan.

### What it is not

The adjacent thing this deliberately excludes, one line each. Without it the first reader to pick the
spec up widens it back to everything it touches. It is the two largest specs that carry the
heading — `second_renderer.md` § Not in scope, `mvp_game_runtime.md` § "What this is *not*, and the
reasoning that says so" — because they are the two with the most adjacent ground to disclaim.

### The trigger

**The heading no standard template has, and the one that makes this a spec rather than a wish.** What
has to happen for the problem to stop being deferrable — an *event*, not a date:

- "The second view. Per-view culling gives every shadow cascade its own visibility buffer" —
  `pso_sort_key.md`
- "A project whose clip sets dominate its load time or its GPU residency" —
  `animation_compression.md`
- Or several, whichever fires first — `vertex_layout_per_submesh.md` lists three.

A spec with no trigger is one of two things, and both are worth catching: work we should be doing
now, or work nobody should be recording. Say the negative too when it applies —
`pso_sort_key.md` ends "Do not do this for the fourteen cases that exist."

Where the answer to "and then what" is short, `## What to do when it fires` is a paragraph and stays
a paragraph. The moment it wants numbered steps it has become a plan, and it waits for the grill.

## 3. Scale it

Four short headings is a complete spec. `animation_compression.md` is 59 lines and answers all seven
questions; `vertex_layout_per_submesh.md` is 71. The two long ones are long because they are
features spanning a subsystem, not because length is a virtue.

**Do not write a spec nobody is waiting on.** A spec that records something merely interesting is a
file every later reader has to open and rule out. The test is the trigger: if you cannot name the
event, there is nothing to defer.

## 4. Where it goes

`docs/specs/<name>.md`, `snake_case`, named for the problem. All eleven are spelled that way; the
plans beside them are mostly kebab-case, which is a habit rather than a rule and not something to
rely on when telling the two directories apart.

The directory is a symlink onto a worktree of the orphan `artefacts` branch, shared by every checkout
in the workspace, and `.claude/hooks/draft_commit.py` commits the file as you write it — so the
feature checkout that would implement it can read it, and a `git pull` that removes it can be undone.
Neither was true when a draft sat untracked in one working directory, which is what
`spec_drafts.md` records.

**It never lands.** There is no pull request that moves a spec onto master, and nothing merges the
branch. It is written, revised, and **deleted on that branch when the thing it describes is built** —
that deletion currently has no owner, so if you are the one who just landed the feature, do it by
hand.

Say where the file is, and stop.

## Rules

- **Never write the plan.** No commit slices, no file lists, no gates, no estimates. The split at the
  top of this file is the whole point of the skill.
- **Every claim carries its line or its number.** A spec is read months later by someone who cannot
  ask you; an unsourced claim is a guess with good posture.
- **Name the standard as an engine and a mechanism, or say you do not know.** Never imply a
  consensus.
- **Record the negative results.** What was tried and measured and dropped is the part nobody can
  re-derive.
- **No trigger, no spec.** If nothing would make it urgent, it is an observation.
- **No status field, no owner, no date.** The file is deleted when the work lands; a spec with a
  status is one somebody is maintaining instead of building.
- **Do not open a pull request.** There is nothing here to propose.
