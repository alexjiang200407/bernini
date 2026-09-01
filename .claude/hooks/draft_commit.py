#!/usr/bin/env python3
"""Commit a spec draft the moment it is written.

`docs/specs/` is a symlink onto a worktree of the `spec-drafts` branch -- an
orphan branch carrying specs and nothing else, because a spec describes code that
does not exist and so is not documentation and is not on master. This is a
PostToolUse hook that commits any write landing inside it.

The commit is the harness's job and not the model's because an ask session has no
shell (`ask_guard.py`), and because a rule saying somebody should commit
afterwards leaves open exactly the window a draft is lost in: on 2026-09-01 an
untracked `docs/specs/second_renderer.md` was deleted by a `git pull` and no
reflog, stash or `restore` knew it had existed.

It runs for every session, not only `ws ask`. A feature agent revising a draft
has the same window, and unlike `ask_guard` this is not a guard rail --
committing somebody's draft costs them nothing.

That agent also has a shell, which is why Bash is matched too. A `sed -i` or a
heredoc names no file the hook could read, so the shell's path ignores the tool
input and looks at the worktree instead. It is gated on a read-only `status`
first, because it runs after every shell command in the repository and nearly
all of them leave the drafts alone.

Bernini must not know where the workspace puts that worktree, so the path is
found by resolving the checkout's own symlink. Everywhere without one -- CI, a
fresh clone, a checkout whose `git clean -xfd` took the link -- the hook is inert.

Exit 2 sends stderr back to the model, which is the only thing worth doing when
the commit fails: the write already happened, so the draft is sitting
uncommitted and silence is what this file exists to end.
"""

import json
import os
import subprocess
import sys
import time

SPECS = os.path.join("docs", "specs")

WRITE_TOOLS = {"Write", "Edit", "MultiEdit", "NotebookEdit"}
SHELL_TOOLS = {"Bash", "PowerShell"}

# Several agents share one worktree, so two drafts written at once collide on
# index.lock. Losing that race is not a failure worth reporting on the first try.
ATTEMPTS = 4
BACKOFF = 0.25

# git here only ever touches a local repository holding a handful of markdown
# files, so anything this slow is stuck rather than working: a lock a killed
# process never released, or a signing prompt with no terminal to answer it. A
# PostToolUse hook is in front of the result the agent is waiting for.
# Overridable so the test for it does not have to wait out the real one.
TIMEOUT = int(os.environ.get("DRAFT_COMMIT_TIMEOUT") or 20)


def contains(parent, child):
    return child == parent or child.startswith(parent + os.sep)


def reason(done):
    return (done.stderr or done.stdout or "git gave no reason").strip()


def git(directory, *args):
    try:
        return subprocess.run(("git", "-C", directory) + args,
                              capture_output=True, text=True, timeout=TIMEOUT)
    except (subprocess.TimeoutExpired, OSError) as error:
        return subprocess.CompletedProcess(args, 1, "", str(error))


def specs_link(root):
    """Where this checkout's specs symlink points, or None when it has none.

    It must resolve *outside* the checkout: a real directory of that name is an
    ordinary part of the tree, already covered by the checkout's own git.

    Deliberately free of subprocesses -- this runs after every write and every
    shell command in the repository, and all but a handful are nowhere near a
    draft.
    """
    resolved = os.path.realpath(os.path.join(root, SPECS))
    if not os.path.isdir(resolved) or contains(os.path.realpath(root), resolved):
        return None
    return resolved


def common_dir(directory):
    """The git directory shared by every worktree of a repository, or None."""
    done = git(directory, "rev-parse", "--git-common-dir")
    shared = done.stdout.strip()
    if done.returncode != 0 or not shared:
        return None
    return os.path.realpath(os.path.join(directory, shared))


def same_repository(root, worktree):
    """Whether the link points at a worktree of *this* repository.

    The whole design turns on it: the drafts branch is bernini's own, which is
    what lets a checkout with no symlink still read a draft with `git show`. It
    is also the only thing standing between a link somebody repointed and a
    commit landing in a repository that never asked for one.
    """
    shared = common_dir(worktree)
    return shared is not None and shared == common_dir(root)


def record(worktree, target):
    """Commit what is already staged for one draft path. None when nothing was."""
    name = os.path.basename(target)
    for attempt in range(ATTEMPTS):
        if git(worktree, "diff", "--cached", "--quiet", "--", target).returncode == 0:
            return None
        # Path-limited, so a draft another agent is midway through staging is not
        # swept into this commit. Unsigned because a signature on a local branch
        # nobody pushes buys nothing and can block on a pinentry.
        done = git(worktree, "-c", "commit.gpgsign=false", "commit",
                   "--quiet", "-m", f"draft: {name}", "--", target)
        if done.returncode == 0:
            return None
        if attempt + 1 < ATTEMPTS:
            time.sleep(BACKOFF)
    return reason(done)


def commit(worktree, target):
    """Stage one draft path and commit it."""
    for attempt in range(ATTEMPTS):
        # -A rather than a plain add, so an Edit that emptied a draft away is
        # recorded as the deletion it is rather than refused as a missing path.
        added = git(worktree, "add", "-A", "--", target)
        if added.returncode == 0:
            return record(worktree, target)
        if attempt + 1 < ATTEMPTS:
            time.sleep(BACKOFF)
    return reason(added)


def sweep(worktree):
    """Commit every draft the shell left uncommitted, one commit each.

    Staged in one pass rather than parsed out of `status`, because a rename there
    is two fields and a non-ascii name is quoted; the index turns both into plain
    paths. Nothing re-stages afterwards -- `git add` refuses a deletion that is
    already in the index.
    """
    staged = git(worktree, "add", "-A", "--", ".")
    if staged.returncode != 0:
        return reason(staged)
    listed = git(worktree, "diff", "--cached", "--name-only", "-z")
    if listed.returncode != 0:
        return reason(listed)
    refused = []
    for name in filter(None, listed.stdout.split("\0")):
        failure = record(worktree, os.path.join(worktree, name))
        if failure is not None:
            refused.append(f"{name}: {failure}")
    return "\n".join(refused) or None


def directory(*candidates):
    """The first candidate that is actually a path."""
    return next(one for one in candidates if isinstance(one, str) and one)


def written(payload, root, drafts):
    """The draft a write tool names, or None when it named something else."""
    tool_input = payload.get("tool_input")
    if not isinstance(tool_input, dict):
        return None
    raw = tool_input.get("file_path") or tool_input.get("notebook_path")
    if not isinstance(raw, str) or not raw:
        return None
    target = os.path.realpath(os.path.join(directory(payload.get("cwd"), root),
                                           os.path.expanduser(raw)))
    return target if contains(drafts, target) else None


def main():
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return 0
    if not isinstance(payload, dict):
        return 0
    tool = payload.get("tool_name")
    if tool not in WRITE_TOOLS and tool not in SHELL_TOOLS:
        return 0

    try:
        root = directory(os.environ.get("CLAUDE_PROJECT_DIR"), payload.get("cwd"), os.getcwd())
        drafts = specs_link(root)
        if drafts is None:
            return 0

        if tool in SHELL_TOOLS:
            # Read-only, and the answer for nearly every command that runs here.
            if not git(drafts, "status", "--porcelain").stdout.strip():
                return 0
            target = drafts
        else:
            target = written(payload, root, drafts)
            if target is None:
                return 0

        if not same_repository(root, drafts):
            return 0
        reason = sweep(drafts) if target is drafts else commit(drafts, target)
    except Exception as error:
        reason = f"{type(error).__name__}: {error}"
        target = SPECS
    if reason is None:
        return 0
    print(f"The draft was written but not committed, so it is only in the working tree:\n"
          f"    {target}\n{reason}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
