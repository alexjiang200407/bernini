---
name: bcp-grill
description: Use before any implementation work begins — interrogates a vague intent until the reason, the standard solution, the fitness of the architecture we have, the boundaries and the acceptance are agreed, then closes on a consensus that goes straight into the plan, where the user confirms it by review. Runs as § 0 of bcp-implement and bcp-feature, and is also invocable alone. Writes nothing until the consensus closes. Triggers: "grill me on X", "bcp-grill", and every bcp-implement or bcp-feature invocation.
---

# Grilling a request

The output is **agreement**, not a document. The document comes afterwards, and it is short because
everything in it was already said out loud.

An agent handed "add X" and left to plan has silently decided a dozen things nobody asked it to
decide: which module, which seam, what state lives where, what "done" means. A clarification pass
over the finished plan cannot recover any of them — it can only ask which blanks to fill in the
blueprint it already drew. This skill runs **before** the blueprint, and its questions are allowed to
destroy the premise.

**Nothing is written until § 3 closes.** No plan, no design, no stub, no file. § 1's survey is the
only thing that happens first, and it reads.

## When the direction is not settled

Grilling narrows. It assumes the direction is roughly known and the job is finding out what it
actually means.

A request with no direction yet — "the build is slow, do something", "we should rethink assets" —
is the other skill's: [bcp-brainstorm](.claude/skills/bcp-brainstorm/SKILL.md) widens a
dissatisfaction into options. Come back here once one is picked. These two are easy to confuse, so
ask when you cannot tell: *"is the direction settled, or do you want options first?"* is one
question and it saves half an hour of grilling the wrong thing.

## 1. Survey — narrow, grounded, and no design

A question from an agent that has not read the code is unanswerable. "Should we reuse the existing
interface?" is not something the user can answer; it is something the *agent* must ask concretely,
naming the interface and the line.

So read first, and read **narrowly**:

- [`bcp-docmap`](.claude/agents/bcp-docmap.md) with the Agent tool, `subagent_type: bcp-docmap`, one
  tier below your own model, asked the question you actually have. It starts at the index in
  [CLAUDE.md](CLAUDE.md), follows the links the docs carry, and returns the answer with the line
  behind it — so the survey costs a paragraph instead of a subsystem.
- The nearest `CLAUDE.md` and the few headers the request obviously touches.
- `ROADMAP.md` § Guiding Constraints, always. It is the fastest way to find out the request fights a
  rule the repo already made.

**Do not design during the survey.** No file lists, no proposed interfaces, no "here is how I would
do it". The moment a solution exists, every later question is measured against it and the grill
becomes a review of your own idea.

Stop as soon as you can ask a grounded question. The survey exists for the *questions*, not for the
answer.

## 2. Grill

Five roots. Every request gets all five, however small, and the middle two in that order:

| | |
|---|---|
| **Why do we need this?** | what breaks today, who it costs, what happens if it is never done |
| **What is the standard solution?** | what a shipping engine does for this problem — the engine and the mechanism, named — with our code out of frame |
| **Is the architecture sufficient?** | whether what we have is the right shape for that, or has to change to be |
| **What are we not doing?** | the boundary — the adjacent thing that is explicitly out |
| **How do we accept this?** | the suite, the tag, the golden image, the assertion that proves it |

The middle two are the ones an agent handed a task never asks for itself, so they get a section
each.

### The standard first, with our code out of frame

Ask what a shipping engine does before looking at what we have. The order runs one way: knowing our
code bends the answer towards whatever would be convenient to build, and knowing the standard cannot
bend our code.

Name it concretely — the engine and the mechanism. *"Unreal's TSR and every standard TAA resolve
carry no per-pixel history beyond colour and depth"* is a claim the user can overturn in a sentence.
*"This isn't really standard"* is not a claim, and it will not survive contact with an
implementation. Where you cannot name the engine and the mechanism, say you do not know rather than
implying a consensus that may not exist.

**The standard is not automatically right.** `ROADMAP.md` rules out clustered/tiled many-light
infrastructure, which is exactly what Unreal and Unity do — a deviation this repo made knowingly and
wrote down. A deviation is fine. A deviation nobody named is the failure, and it is a different
thing: it arrives as a mechanism no other engine has a word for, defended one bug at a time, until
the feature that assumes the standard shape lands on top of it.

That is the case study. The TAA resolve carried a per-pixel variance store widening the clamp box at
rest, machinery no standard resolve has, and it held until skinned meshes moved faster than a 3×3
neighbourhood could witness — removed in `f637fa7d` at the cost of a refactor, where the question
here costs a line. [docs/taa.md](docs/taa.md) records the recipe it returned to.

So when the answer is *we are deviating*, that is **its own ADR** in § 3, with the standard named as
the alternative it rejects. It is the line [`bcp-precheck`](.claude/agents/bcp-precheck.md) § 3 reads
the diff back against, and the only form of it a diff can be checked against.

### Is the architecture sufficient, or are we building on top of it?

Not *can this be made to work*. It always can, and an agent handed a task will find the way — that is
the failure, not the fix. The question is whether the mechanism this needs already has a home, and
whether that home is the right shape for it.

When it is not, the bias here is to **change what exists rather than layer over it**. A workaround
fitted around a wrong shape is cheaper this week and is what the next feature breaks on. Say which of
the two is being proposed, so the user picks rather than receiving the second by default.

Changing it is not an argument against the change: a refactor that enables a feature is its own
commit ahead of the feature ([bcp-implement § 3](.claude/skills/bcp-implement/SKILL.md)), and the
decision to make it is an ADR like any other.

Then the lenses this repo makes worth asking. Use the ones the survey lit up; skip the rest:

- **Does it already exist?** Grep `core` and the subsystem by *behaviour*, not by name — the
  duplicate is never called the same thing. [`bcp-precheck`](.claude/agents/bcp-precheck.md) § 2 asks
  this after the code is written, which is the expensive time to find out.
- **Which layer owns it?** `bgl` never links `assetlib`, `assetlib` never links `bgl`, `gamelib` is
  the seam. A request that needs a violation is a wrong request — find the seam and say so.
- **Which Guiding Constraint does it touch?** GPU-driven by default, one dominant light, instances
  as the unit of scale, an API-agnostic RHI, the IDL as single source of truth. Breaking one is a
  decision about the roadmap, not about this change, and it is the user's to make knowingly.
- **Does the premise survive?** The strongest outcome of a grill is *we do not need this*. Say it
  when the survey supports it.
- **What is the cheapest thing that would prove this wrong?** If nothing could, the acceptance
  criteria are theatre.

### How to ask

Ask in **small batches** — three or four, then listen. Twenty at once is a form, and forms get
skimmed; the point is that an answer redirects the next question.

Structured forks — reuse this or build that, A or B — go through the AskUserQuestion tool, with the
trade-off written into each option so the choice is informed. Open questions go in chat as prose.
Carry a recommendation into every fork: a question with no opinion behind it makes the user do the
agent's thinking.

Every question must be **answerable by the user** — about intent, priority, boundary or cost. A
question whose answer is in the code is one you should have surveyed.

**Push back.** When an answer contradicts the survey, say so and name the line. The grill is worth
having only because it can overturn the request; an agent that agrees with everything is a slower
way of guessing.

### Even a two-line fix

Every invocation is grilled. There is no size threshold and no escape hatch — a small change gets a
small grill, the five roots in one batch, often one line each. Short is fine; empty is not — an
answer to the middle two still names the thing, even when the thing is *the standard here is the
path we already have and the bug is in it*. A stock *n/a, it's a small fix* is a skip wearing an
answer's clothes, and the shelter was a small fix. That is a minute, and it is the pass that catches
*this fix is at the wrong layer* while the fix is still hypothetical.

## 3. Close the grill

The grill ends when **every question has an answer and you can state the consensus back** — not
when you run out of questions, and not on a chat reply to the summary.

State it as four headings, around ten lines, no implementation:

```
Context     — what breaks today, and why now
Decisions   — ADR-1 …, each with the alternative rejected, one line apiece
Non-goals   — what this is explicitly not doing
Acceptance  — the gate that proves it: suite, tag, golden image, assertion
```

A deviation from the standard named in § 2 is **an ADR of its own**, with the standard as the
alternative it rejects. Anywhere else it is invisible to the diff, and
[`bcp-precheck`](.claude/agents/bcp-precheck.md) § 3 has nothing to read the change back against.

Then **do not ask for confirmation** — write it. The caller puts it down as the head of
`docs/plans/<name>.md` and carries on: see [bcp-implement § 0](.claude/skills/bcp-implement/SKILL.md)
and [bcp-feature § 0](.claude/skills/bcp-feature/SKILL.md). Invoked alone, the grill stops here and
the consensus stays in chat for whichever skill runs next.

The user confirms it **where it lands, by review**: the plan is the first commit of the PR under
bcp-implement and its own PR under bcp-feature, so the boundaries are the first thing a reviewer
reads and a wrong one comes back as a review comment, which [bcp-revise](.claude/skills/bcp-revise/SKILL.md)
acts on. Waiting for a chat "yes" first was tried and dropped: it tests only whether the summary is
faithful to a conversation that already happened — the part a model gets wrong least — and it left
unattended sessions idle on a question that was not one. The *questions* still block (below); the
summary of their answers does not.

## No human, no grill

**When nobody is at the keyboard, stop and wait.** An unattended agent — `ws feature` started
non-interactively, a `--continue` resume with no user present — asks its questions, says plainly that
it is blocked on them, and ends the turn. It does not answer them itself and it does not proceed on
assumptions.

That is deliberate, and the cost is real: such a session idles until someone attaches. The
alternative — an agent that invents the answers and labels them "unconfirmed" — produces exactly the
assumptions this skill exists to prevent, and they are the ones that survive into the plan unread.

## Rules

- **Write nothing before § 3 closes.** Not a plan, not a stub, not a file.
- **Never answer your own questions.** Ask, then wait.
- **Never wait on the summary.** Once the questions are answered, the consensus is written, not
  asked; the user confirms it by reviewing the plan.
- **Never grill from an unread repo,** and never design during the survey.
- **Name the standard before you look at our architecture,** and name it as an engine and a
  mechanism or not at all. A deviation is a decision; a deviation nobody named is a bug with a long
  fuse.
- **Ask in batches**, and let each answer change the next question.
- **A grill that overturns the request succeeded.** "We do not need this" is the cheapest outcome
  available.
- **No implementation steps in the consensus.** Decisions and boundaries only; the tasks come after,
  and they are the caller's.
