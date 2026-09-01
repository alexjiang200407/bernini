#!/usr/bin/env python3
"""Hold an ask session to reading.

A PreToolUse hook on the editing tools and on Bash, inert unless WS_ASK is set --
which only `ws ask` does. An ask session runs in the *main clone*, the checkout
every quick fix is cut from and every feature lands into, and it sits beside
every other worktree in the workspace sharing one object store. So a question
that writes anywhere costs somebody else a confusing `git status`, a refused
fast-forward, or an edit appearing in a feature branch nobody made it on.

Two rules, and the second is the short one: **there is no shell**. Bash is
refused outright rather than inspected, because inspecting it does not converge.
Eight rounds of review said so -- a guard that reads the command line has to find
the write inside `echo $(git log; git push)`; one that allowlists commands still
owes an answer for `find -exec`, `sort -o`, `uniq out`, `git diff --output=`; and
one that allowlists a single command with a flag rule is undone by
`--{output=x,z}`, which is not `--output=` until bash expands it. Each fix was
right and the next spelling was somewhere else. Refusing the shell has no next
spelling.

What that costs is `git log` -- history, and the commit message behind a
decision. It is a real loss and a smaller one here than elsewhere: `docs/plans/`
keeps an ADR per change precisely so the reasoning is a file somebody can open,
and Read, Grep and Glob open files without a shell to guard.

The other rule is the write allowlist: `docs/specs/*.md` and a temp directory
outside the checkout. Refusing "anywhere but this checkout" instead would sound
friendlier and is the hole -- what it waves through is the sibling worktrees the
other agents are working in.

`docs/specs` is normally a symlink onto a worktree of the `spec-drafts` branch,
which lives outside every checkout, so the root is resolved before anything is
matched against it. Where the workspace has not seeded that link the same
resolution lands back inside the checkout, and a spec written there is untracked
until `ws init` collects it -- worse than a commit, and better than an ask
session with nowhere at all to write.

It is a guard rail against a brief that was misread, not a sandbox against
someone trying to leave. Exit 2 blocks the call and sends stderr back to the
model; exit 0 allows it.
"""

import json
import os
import sys
import tempfile

SPECS = os.path.join("docs", "specs")

WRITE_TOOLS = {"Write", "Edit", "MultiEdit", "NotebookEdit"}

SPEC_ONLY = ("The one thing an ask session may write is a spec:\n"
             "    docs/specs/<name>.md\n"
             "one file for a problem we have decided not to solve yet -- what it is, the trigger\n"
             "that makes it urgent, and the design already settled on. It is committed for you on\n"
             "the spec-drafts branch and never lands on master. A temporary directory is the only\n"
             "other place a write is allowed; every other path, in this checkout or any worktree\n"
             "beside it, is refused.")

NO_SHELL = ("An ask session has no shell. Not this command in particular -- Bash at all, because a\n"
            "guard that reads a command line to find the write in it has never once been finished.\n"
            "Everything a question needs is already a tool: Read opens a file, Grep searches the\n"
            "tree, Glob finds one. None of them is a shell, so none of them is guarded.\n"
            "What is genuinely gone is git: no log, no blame, no diff. For why a thing is the way\n"
            "it is, read `docs/plans/` -- one ADR per change, each decision with the alternative it\n"
            "rejected -- and the `docs/` page for the subsystem.\n"
            "If the answer needs history, or is a change, say so and stop. A checkout of its own is\n"
            "what that wants: the user starts one with `ws feature <name> \"<what to build>\"`, and\n"
            "`ws cmd bernini -- claude` opens an unguarded session in this same clone.")


def contains(parent, child):
    return child == parent or child.startswith(parent + os.sep)


def temp_roots(root):
    """Where a scratch file may go: a temp directory that is not inside the checkout.

    TMPDIR is somebody's environment, not a fact. One pointed at the checkout --
    a stray profile, an IDE, a CI runner -- would otherwise turn the allowlist off
    for everything under it, silently.
    """
    found = {tempfile.gettempdir(), "/tmp", "/private/tmp", "/var/tmp"}
    for name in ("TMPDIR", "TMP", "TEMP"):
        if os.environ.get(name):
            found.add(os.environ[name])
    resolved = {os.path.realpath(path) for path in found}
    return {path for path in resolved if not contains(root, path)}


def project_root(payload):
    root = os.environ.get("CLAUDE_PROJECT_DIR") or payload.get("cwd") or os.getcwd()
    return os.path.realpath(root)


def verdict_for_path(root, cwd, raw):
    """None when the path may be written, else the message saying why not."""
    target = os.path.realpath(os.path.join(cwd, os.path.expanduser(raw)))
    specs = os.path.realpath(os.path.join(root, SPECS))
    if target.endswith(".md") and contains(specs, target):
        return None
    if any(contains(temp, target) for temp in temp_roots(root)):
        return None
    return f"{SPEC_ONLY}\n\nRefused: {raw}"


def refuse(message):
    print(f"Blocked by .claude/hooks/ask_guard.py.\n\n{message}", file=sys.stderr)
    return 2


def main():
    if not os.environ.get("WS_ASK"):
        return 0
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return 0
    if not isinstance(payload, dict):
        return 0

    tool = payload.get("tool_name") or ""
    if tool in ("Bash", "PowerShell"):
        return refuse(NO_SHELL)
    if tool not in WRITE_TOOLS:
        return 0

    try:
        tool_input = payload.get("tool_input") or {}
        target = tool_input.get("file_path") or tool_input.get("notebook_path")
        if not isinstance(target, str) or not target:
            return refuse(SPEC_ONLY)
        root = project_root(payload)
        message = verdict_for_path(root, payload.get("cwd") or root, target)
    except Exception:
        # Only exit 2 blocks a tool call; every other status is an error the
        # harness reports while letting the call through. So a shape this did not
        # expect has to end in a refusal and not in a traceback -- a crash here is
        # a write allowed, which is the one outcome nothing else in this file
        # permits.
        return refuse(SPEC_ONLY)
    return refuse(message) if message else 0


if __name__ == "__main__":
    sys.exit(main())
