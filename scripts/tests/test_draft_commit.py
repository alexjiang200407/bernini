"""What happens to a spec draft the moment it is written.

`.claude/hooks/draft_commit.py` is a PostToolUse hook, so the write has already
happened by the time it runs. Exit 2 undoes nothing -- it only tells the model the
draft is sitting uncommitted, which is the state the hook exists to end. Exit 0 is
silence, and it is the right answer for every checkout that has no drafts worktree
linked into it.

The hook is run as a subprocess rather than imported, because that is how Claude
Code runs it -- a payload on stdin and an exit status back.
"""

import json
import os
import subprocess
import sys
import time

import pytest

REPORTED = 2
SILENT = 0

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOOK = os.path.join(ROOT, ".claude", "hooks", "draft_commit.py")


def git(directory, *args):
    return subprocess.run(("git", "-C", str(directory)) + args,
                          capture_output=True, text=True, check=True)


def run(root, tool="Write", **tool_input):
    payload = json.dumps({"tool_name": tool, "cwd": str(root), "tool_input": tool_input})
    env = dict(os.environ, CLAUDE_PROJECT_DIR=str(root))
    return subprocess.run([sys.executable, HOOK], input=payload, env=env,
                          capture_output=True, text=True)


def log(worktree):
    """The draft commits on the branch, newest first.

    The parentless root every orphan branch hangs from is dropped: it is scaffolding,
    not a draft, and every assertion here is about what the hook added above it.
    """
    done = subprocess.run(("git", "-C", str(worktree), "log", "--format=%s"),
                          capture_output=True, text=True)
    return done.stdout.splitlines()[:-1]


def checkout(root, branch="master"):
    """A repository with one commit, so it has a HEAD to hang a worktree off."""
    (root / "docs" / "specs").mkdir(parents=True)
    git(root, "init", "--quiet", "--initial-branch", branch)
    git(root, "config", "user.email", "drafts@example.invalid")
    git(root, "config", "user.name", "drafts")
    (root / "README.md").write_text("x\n")
    git(root, "add", "README.md")
    git(root, "commit", "--quiet", "-m", "root")
    return root


def orphan_worktree(root, path, branch="spec-drafts"):
    """The branch exactly as `ws _drafts` builds it: an empty tree, a parentless
    commit, a ref, and one worktree — never a checkout, so it carries no sources."""
    tree = git(root, "hash-object", "-w", "-t", "tree", os.devnull).stdout.strip()
    commit = git(root, "commit-tree", tree, "-m", "root").stdout.strip()
    git(root, "branch", branch, commit)
    git(root, "worktree", "add", str(path), branch)
    return path


@pytest.fixture
def linked(tmp_path):
    """A checkout whose docs/specs/drafts links to a worktree of its *own* repository.

    That is the real topology and not a detail: the branch being bernini's own is
    what lets a checkout with no symlink read a draft with `git show`, and what the
    hook checks before committing anywhere.
    """
    root = checkout(tmp_path / "checkout")
    drafts = orphan_worktree(root, tmp_path / "spec-drafts")
    (root / "docs" / "specs" / "drafts").symlink_to(drafts)
    return root, drafts


def write(root, name, text):
    path = root / "docs" / "specs" / "drafts" / name
    path.write_text(text)
    return str(path)


def test_a_draft_is_committed_where_it_lands(linked):
    root, drafts = linked
    assert run(root, file_path=write(root, "vat.md", "# vat\n")).returncode == SILENT
    assert log(drafts) == ["draft: vat.md"]
    assert git(drafts, "status", "--porcelain").stdout == ""


def test_every_revision_is_its_own_commit(linked):
    """One commit per write is what makes the branch a recovery mechanism rather than a
    backup: the version before the one that was wrong is a `git show` away."""
    root, drafts = linked
    run(root, file_path=write(root, "vat.md", "# vat\n"))
    run(root, file_path=write(root, "vat.md", "# vat\n\nmore\n"))
    assert log(drafts) == ["draft: vat.md", "draft: vat.md"]


def test_a_write_that_changed_nothing_leaves_no_empty_commit(linked):
    root, drafts = linked
    run(root, file_path=write(root, "vat.md", "# vat\n"))
    assert run(root, file_path=write(root, "vat.md", "# vat\n")).returncode == SILENT
    assert log(drafts) == ["draft: vat.md"]


def test_one_draft_does_not_carry_another_into_its_commit(linked):
    """Several agents share the one worktree, so a draft another session is midway
    through writing must not be swept into this one's commit."""
    root, drafts = linked
    write(root, "other.md", "half written\n")
    run(root, file_path=write(root, "vat.md", "# vat\n"))
    assert log(drafts) == ["draft: vat.md"]
    assert "other.md" in git(drafts, "status", "--porcelain").stdout


def test_a_checkout_with_no_drafts_link_is_untouched(tmp_path):
    """CI, a fresh clone, and every checkout whose `git clean -xfd` took the symlink.
    Bernini knows nothing about a workspace that puts a worktree beside it, so the
    absence of the link is the whole of the test for whether this applies."""
    root = tmp_path / "checkout"
    (root / "docs" / "specs").mkdir(parents=True)
    target = root / "docs" / "specs" / "vat.md"
    target.write_text("# vat\n")
    assert run(root, file_path=str(target)).returncode == SILENT


def test_a_real_directory_of_that_name_is_left_to_the_checkouts_own_git(tmp_path):
    """A drafts directory that resolves inside the checkout is an ordinary part of the
    tree, and committing it here would commit into the wrong repository."""
    root = tmp_path / "checkout"
    drafts = root / "docs" / "specs" / "drafts"
    drafts.mkdir(parents=True)
    target = drafts / "vat.md"
    target.write_text("# vat\n")
    assert run(root, file_path=str(target)).returncode == SILENT


def test_a_link_to_something_that_is_not_a_repository_commits_nothing(tmp_path):
    """The link is seeded by the workspace and could point anywhere -- a half-made setup,
    a directory somebody replaced. There is nowhere to commit, and saying so would be
    reporting the workspace's problem on every write."""
    root = checkout(tmp_path / "checkout")
    plain = tmp_path / "not-a-repo"
    plain.mkdir()
    (root / "docs" / "specs" / "drafts").symlink_to(plain)
    target = plain / "vat.md"
    target.write_text("# vat\n")
    assert run(root, file_path=str(target)).returncode == SILENT


def test_a_link_into_another_repository_commits_nothing(tmp_path):
    """A draft belongs on a branch of the repository it documents. Committing into
    whatever repository the link happens to reach would put it somewhere nobody
    looking for it would ever read, and somewhere `git show` here cannot."""
    root = checkout(tmp_path / "checkout")
    stranger = checkout(tmp_path / "stranger", branch="main")
    (root / "docs" / "specs" / "drafts").symlink_to(stranger)
    target = stranger / "vat.md"
    target.write_text("# vat\n")
    assert run(root, file_path=str(target)).returncode == SILENT
    assert log(stranger) == []


def test_a_write_outside_the_worktree_is_not_this_hooks_business(linked):
    root, drafts = linked
    (root / "libs").mkdir()
    target = root / "libs" / "Renderer.cpp"
    target.write_text("int main() {}\n")
    assert run(root, file_path=str(target)).returncode == SILENT
    assert log(drafts) == []


def test_the_reading_tools_are_never_in_the_way(linked):
    root, _ = linked
    for tool in ("Read", "Grep", "Glob", "Task"):
        assert run(root, tool=tool, file_path=write(root, "vat.md", "# vat\n")).returncode == SILENT


# --- the shell ---------------------------------------------------------------
#
# A feature agent has a shell where an ask session does not, and a `sed -i` or a
# heredoc names no file in the tool input. Leaving that uncovered would reopen
# the uncommitted-and-invisible state for the one session type that shares the
# loss window with an ask session.

def test_a_draft_written_by_the_shell_is_committed_too(linked):
    root, drafts = linked
    write(root, "vat.md", "# vat\n")
    assert run(root, tool="Bash", command="sed -i '' s/x/y/ docs/specs/drafts/vat.md").returncode == SILENT
    assert log(drafts) == ["draft: vat.md"]


def test_the_shell_path_reads_the_worktree_and_not_the_command(linked):
    """The command line is never parsed -- that is the guard's abandoned approach, and
    a draft can be written by a composition no parser sees. What changed is looked up."""
    root, drafts = linked
    write(root, "one.md", "# one\n")
    write(root, "two.md", "# two\n")
    assert run(root, tool="Bash", command="true").returncode == SILENT
    assert sorted(log(drafts)) == ["draft: one.md", "draft: two.md"]


def test_a_shell_command_that_touched_no_draft_commits_nothing(linked):
    root, drafts = linked
    assert run(root, tool="Bash", command="ls").returncode == SILENT
    assert log(drafts) == []


def test_a_deletion_by_the_shell_is_recorded(linked):
    """`rm` is how the 2026-09-01 loss looked from the outside. The commit is what makes
    it recoverable -- the version before it is still on the branch."""
    root, drafts = linked
    run(root, file_path=write(root, "vat.md", "# vat\n"))
    (drafts / "vat.md").unlink()
    assert run(root, tool="Bash", command="rm docs/specs/drafts/vat.md").returncode == SILENT
    assert log(drafts) == ["draft: vat.md", "draft: vat.md"]
    assert "vat.md" in git(drafts, "show", "--name-only", "--format=", "HEAD~1").stdout


def test_a_notebook_carries_its_path_under_another_name(linked):
    root, drafts = linked
    path = write(root, "vat.md", "# vat\n")
    assert run(root, tool="NotebookEdit", notebook_path=path).returncode == SILENT
    assert log(drafts) == ["draft: vat.md"]


def test_a_commit_that_fails_is_reported_and_not_swallowed(linked):
    """Silence about an uncommitted draft is exactly the failure this file exists to
    prevent, so the one thing worth doing on the way out is saying so."""
    root, drafts = linked
    hooks = drafts / "hooks"
    hooks.mkdir()
    refuse = hooks / "pre-commit"
    refuse.write_text("#!/bin/sh\nexit 1\n")
    refuse.chmod(0o755)
    git(drafts, "config", "core.hooksPath", "hooks")

    done = run(root, file_path=write(root, "vat.md", "# vat\n"))
    assert done.returncode == REPORTED
    assert "vat.md" in done.stderr
    assert log(drafts) == []


def test_git_that_hangs_is_not_allowed_to_hold_the_turn(linked, monkeypatch):
    """A PostToolUse hook sits in front of the result the agent is waiting for, and a
    commit is the one call here that can block rather than fail -- a stale lock, or a
    signing prompt with no terminal to answer it."""
    root, drafts = linked
    hooks = drafts / "hooks"
    hooks.mkdir()
    stall = hooks / "pre-commit"
    stall.write_text("#!/bin/sh\nsleep 5\n")
    stall.chmod(0o755)
    git(drafts, "config", "core.hooksPath", "hooks")

    started = time.monotonic()
    env = dict(os.environ, CLAUDE_PROJECT_DIR=str(root), DRAFT_COMMIT_TIMEOUT="1")
    payload = json.dumps({"tool_name": "Write", "cwd": str(root),
                          "tool_input": {"file_path": write(root, "vat.md", "# vat\n")}})
    done = subprocess.run([sys.executable, HOOK], input=payload, env=env,
                          capture_output=True, text=True)
    assert done.returncode == REPORTED
    assert time.monotonic() - started < 15
    assert log(drafts) == []


@pytest.mark.parametrize("payload", [
    '{"tool_name":"Write","tool_input":{"file_path":42}}',
    '{"tool_name":"Write","tool_input":["docs/specs/drafts/x.md"]}',
    '{"tool_name":"Write"}',
    '{"tool_name":"Write","cwd":7,"tool_input":{"file_path":"x.md"}}',
    'not json at all',
    '[]',
])
def test_a_shape_it_did_not_expect_says_nothing(payload):
    """This hook fails open where `ask_guard.py` fails closed, and the asymmetry is the
    point: a guard that crashes has allowed a write, while this one has only failed to
    commit a file that is not a draft."""
    env = dict(os.environ, CLAUDE_PROJECT_DIR=ROOT)
    done = subprocess.run([sys.executable, HOOK], input=payload, env=env,
                          capture_output=True, text=True)
    assert done.returncode == SILENT
