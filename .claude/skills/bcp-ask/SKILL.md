---
name: bcp-ask
description: Use when the request is a question about bernini rather than a change to it — how something works, why it was built that way, where a rule lives, whether something is a problem. Reads the code and the docs and answers with the line behind each claim; writes nothing but a spec, and only when the answer is a problem worth recording. Triggers: "ws ask", "bcp-ask", "how does X work", "why does Y do Z", "where is W decided", "is V a problem?".
---

# Answering a question about bernini

The output is an **answer**, not a change. This skill runs in the main clone — the checkout every
quick fix is cut from and every feature lands into — so it reads, and the one thing it may write is
a spec.

`.claude/hooks/ask_guard.py` enforces that while `ws ask` is what started the session, and it is two
rules. **Writes**: `docs/specs/*.md` — normally a symlink resolving outside the checkout — and a
temporary directory; every other path is refused, in this checkout and in every worktree beside it. **There is no shell**: Bash is refused outright, not inspected, because a guard
that reads a command line to find the write in it has never once been finished.

So the tools are the whole toolkit. Read opens a file, Grep searches the tree, Glob finds one; none
is a shell, so none is guarded, and between them they are most of what a question needs. Being
blocked is not a puzzle to route around — it means the answer wants history or has turned into a
change, and § 2 and § 4 say where each of those goes.

## 1. Know which question you were asked

Three kinds arrive through the same sentence, and they end in different places:

| | |
|---|---|
| **fact** | "what writes `.bvat`?" — answered from the code, with the line. |
| **judgement** | "is the 60 MB clip a problem?" — answered with the measurement, or with the fact that nobody has measured. |
| **change** | "make the bake faster" — not this skill's. See § 4. |

A change dressed as a question is the common one, and taking it at face value produces a confident
essay nobody asked for. When you cannot tell, ask — one line, and it costs nothing. A question whose
*direction* is undecided — "what are the options for X" — is
[bcp-brainstorm](.claude/skills/bcp-brainstorm/SKILL.md)'s, not this one's.

## 2. Survey before answering

Never answer a bernini question from memory. The repo moves weekly, and a plausible answer about
code that no longer exists is worse than no answer.

Start at the Documentation Index in [CLAUDE.md](CLAUDE.md). When the question spans more than one
doc, spawn [`bcp-docmap`](.claude/agents/bcp-docmap.md) with the Agent tool,
`subagent_type: bcp-docmap`, one tier below your own model, and ask it the question you actually
have — it follows the links the docs carry into further docs and into the headers they defer to, and
reports with the line behind each claim.

Then read the **source**, because a doc says what was true when it was written.

For *why*, `docs/plans/` is the record: one ADR per change, each decision written down with the
alternative it rejected, and — unlike a commit message — a file you can open. Read it before
concluding that a design was accidental. There is no `git log` here, so when the answer genuinely
turns on history rather than on the tree, say that plainly and name what would need to be looked up.
`ws cmd bernini -- claude` opens an unguarded session in this same clone for someone who needs it.

## 3. Answer

Short, and every claim carries `file:line` — that is what makes an answer checkable rather than
believable.

Answer the question that was asked. A question about one pass is not an invitation to review the
subsystem, and an answer that opens with three paragraphs of context is one the reader has to mine.

**Say when you do not know.** "Nothing in the tree measures this" is an answer, and a useful one;
inventing the number is the single most expensive thing this skill can do. Distinguish what you
read from what you inferred — mark the inference as one.

## 4. When the answer is a change

Stop and say so. The main clone is not where changes are made, and a session started to answer a
question has had no grill, no plan and no branch.

Name the change in a sentence and give the user the command. **You do not run it** — starting a
feature from inside a question is how one session becomes three, and the guard refuses it:

```sh
ws feature <name> "<what to build>"        # its own checkout, its own branch, its own PR
```

## 5. When the answer is a problem

A question whose answer is *"that is a real problem, and we are not solving it today"* is exactly
what [CLAUDE.md](CLAUDE.md) § Specs describes: one file per problem we have decided not to solve
yet, so nobody re-derives it. That, and only that, is what this skill writes.

Write `docs/specs/<name>.md`, and keep it to the three things a spec is for:

```markdown
# <the problem>

## What it is
<the problem, and the measurement or the code that shows it>

## The trigger
<what has to happen for this to stop being deferrable>

## The design
<what has already been settled, with the alternative each part rejected>
```

Do not write one for a problem that is merely interesting. A spec that nobody is waiting on is a
file the next reader has to rule out.

`docs/specs/` is a worktree of the orphan `spec-drafts` branch, shared by every checkout in the
workspace, and `.claude/hooks/draft_commit.py` commits the file as you write it — so the feature
checkout that would implement the spec can read it, and a `git pull` that removes it can be undone.
Neither happens for a file sitting untracked in one working directory, which is what this used to be.

**It never lands.** A spec describes code that does not exist, so it is not documentation and there
is no pull request that moves it onto master; it is written, revised, and deleted on that branch when
the thing it describes is built. Say where the file is, and stop.

## Rules

- **Read, do not change.** The clone is shared with every feature that lands. If the answer is a
  change, § 4.
- **Never commit, push, or open a pull request.** There is no branch here, and nothing to propose.
- **Every claim carries its line.** An answer without one is a guess with good posture.
- **Do not pad.** A one-line question can have a one-line answer, and usually should.
