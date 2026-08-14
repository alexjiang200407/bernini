---
name: bcp-brainstorm
description: Use when the direction itself is undecided — widens a vague dissatisfaction into options that differ in kind, each with its cost and what it forecloses, until one can be picked. The counterpart to bcp-grill, which narrows a direction already settled. Produces no plan and no code. Triggers: "brainstorm X", "what are the options for Y", "the build is slow, what could we do", any request whose goal is clear but whose approach is not.
---

# Brainstorming a direction

Widening, not narrowing. This skill runs when the *goal* is known and the *approach* is not.

It is the easiest skill in this repo to confuse with [bcp-grill](.claude/skills/bcp-grill/SKILL.md),
so the split is worth stating once:

| | |
|---|---|
| **bcp-brainstorm** | direction unknown. Opens the range of options. Ends when one is picked. |
| **bcp-grill** | direction known. Interrogates what it actually means. Ends when the boundaries are agreed. |

Grilling a request whose direction is not settled produces confident answers about the wrong thing.
Brainstorming a settled one wastes the user's afternoon relitigating a decision they already made.
When you cannot tell which you are in, ask — one question, and it costs nothing.

**Nothing is written and nothing is built here.** The output is a chosen direction, in a paragraph.

## 1. Find out what is actually wrong

A brainstorm that starts from a proposed solution has already narrowed. Start from the
dissatisfaction:

- What is the observed symptom, in the user's own terms? Not the diagnosis — the symptom.
- What has already been tried, and how did it fail? A rejected option is worth more than a new one.
- What is the constraint that makes the obvious answer unavailable? There usually is one, and it is
  usually the whole problem.

Then survey, exactly as [bcp-grill § 1](.claude/skills/bcp-grill/SKILL.md) does: narrow, grounded,
`ROADMAP.md` § Guiding Constraints always, and no design. Options invented without reading the code
differ only in vocabulary.

## 2. Widen

**At least three options, and they must differ in kind.** Three settings of the same dial is one
option wearing three hats — that is the failure mode this skill exists to avoid, and it is the one
an agent falls into by default because variations are easy to generate.

Two are almost always worth including, and are almost always missing:

- **Do nothing.** Name what it actually costs to live with the problem. Often less than the fix.
- **Reuse what exists.** The option where nothing new is built and something already in `core` or
  the subsystem is bent to fit. It is the cheapest option and the one nobody proposes.

For each option, say four things and no more:

| | |
|---|---|
| **What it is** | one sentence |
| **What it costs** | the work, and what it makes permanently harder |
| **What it forecloses** | the roadmap item or future change it rules out |
| **How you would know it was wrong** | the cheapest signal, and how early it arrives |

An option that breaks a layering rule or a Guiding Constraint is **named, not dropped**. The user may
want to change the rule, and that decision belongs to them — but present it as what it is, with the
rule quoted.

## 3. Converge

Widening without a recommendation just moves the work. Once the options are on the table, say which
one you would take and why, in a few lines — then let the user pick.

The moment a direction is chosen, hand off to [bcp-grill](.claude/skills/bcp-grill/SKILL.md), which
turns it into agreed boundaries and an acceptance gate. The brainstorm's own output goes no further
than chat: the *why this direction over the others* is worth keeping, and it is kept as an ADR line
in the grill's consensus, not as a document of its own.

## Rules

- **Never converge in § 2.** Recommending while still generating collapses the range to the first
  idea.
- **Never present variations as options.** If two differ only by a parameter, they are one.
- **Always include do-nothing and reuse-what-exists.**
- **Name the rule an option breaks; do not silently drop the option.**
- **Write no plan and no code.** This skill ends in a decision, and bcp-grill picks it up.
