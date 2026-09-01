"""What an ask session may and may not do.

`.claude/hooks/ask_guard.py` is the only thing between a question asked in the
main clone and an edit left in the checkout every quick fix is cut from. Exit 2
blocks the tool call; exit 0 allows it.

The hook is run as a subprocess rather than imported, because that is how Claude
Code runs it -- a payload on stdin and an exit status back.
"""

import json
import os
import subprocess
import sys

import pytest

BLOCKED = 2
ALLOWED = 0

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOOK = os.path.join(ROOT, ".claude", "hooks", "ask_guard.py")


def verdict(tool, tool_input, asking=True, root=ROOT, **environment):
    env = dict(os.environ, CLAUDE_PROJECT_DIR=root, **environment)
    env.pop("WS_ASK", None)
    if asking:
        env["WS_ASK"] = "1"
    payload = json.dumps({"tool_name": tool, "cwd": root, "tool_input": tool_input})
    done = subprocess.run([sys.executable, HOOK], input=payload, env=env,
                          capture_output=True, text=True)
    return done.returncode


def test_a_session_that_is_not_an_ask_is_untouched():
    """Without WS_ASK the hook is inert -- every other session in the repo works normally."""
    assert verdict("Write", {"file_path": "libs/bgl_extended/src/x.cpp"}, asking=False) == ALLOWED
    assert verdict("Bash", {"command": "git commit -m x"}, asking=False) == ALLOWED
    assert verdict("Bash", {"command": "git log --oneline"}, asking=False) == ALLOWED


def test_the_reading_tools_are_never_in_the_way():
    """Read, Grep and Glob are how an ask session works, and the hook does not see them."""
    for tool in ("Read", "Grep", "Glob", "Task", "WebFetch"):
        assert verdict(tool, {"file_path": "libs/bgl_extended/src/Renderer.cpp"}) == ALLOWED


# --- writes -----------------------------------------------------------------

@pytest.mark.parametrize("path, expected", [
    ("libs/bgl_extended/src/Renderer.cpp", BLOCKED),
    ("apps/editor/src/Window.cpp", BLOCKED),
    ("CLAUDE.md", BLOCKED),
    ("docs/vat.md", BLOCKED),                       # a doc is not a spec
    ("docs/specs/notes.txt", BLOCKED),              # a spec is markdown
    ("docs/specs/animation_compression.md", ALLOWED),
    ("docs/specs/new_problem.md", ALLOWED),
    ("/tmp/scratch.md", ALLOWED),                   # a scratch file is nobody's checkout
])
def test_only_a_spec_may_be_written(path, expected):
    assert verdict("Write", {"file_path": path}) == expected
    assert verdict("Edit", {"file_path": path}) == expected
    assert verdict("MultiEdit", {"file_path": path}) == expected


@pytest.mark.parametrize("path", [
    "../ask/libs/bgl_extended/src/Renderer.cpp",             # another feature's worktree
    "../../bernini-test-project/README.md",         # the project landing clone
    "../../ws",                                     # the workspace tooling itself
    "~/.zshrc",
    "docs/specs/../../libs/x.cpp",                  # ...and no walking back out
])
def test_a_worktree_beside_this_one_is_not_fair_game(path):
    """Writes are an allowlist. A denylist of "not this checkout" waves every sibling
    through, and the workspace is nothing but siblings sharing one object store."""
    assert verdict("Write", {"file_path": path}) == BLOCKED


def test_a_temp_root_inside_the_checkout_does_not_open_it():
    """TMPDIR is somebody's environment, not a fact. Pointed at the checkout it would turn
    the allowlist off for everything under it, silently."""
    assert verdict("Write", {"file_path": "libs/bgl_extended/src/x.cpp"}, TMPDIR=ROOT) == BLOCKED


def test_a_write_with_no_path_is_not_a_reason_to_allow_it():
    assert verdict("Write", {"file_path": ""}) == BLOCKED


def test_a_notebook_carries_its_path_under_another_name():
    assert verdict("NotebookEdit", {"notebook_path": "libs/x.ipynb"}) == BLOCKED
    assert verdict("NotebookEdit", {"notebook_path": "docs/specs/x.md"}) == ALLOWED


# --- the drafts directory ---------------------------------------------------
#
# `docs/specs/drafts` is a symlink onto a worktree of the spec-drafts branch,
# shared by every checkout and therefore living outside all of them. Resolving it
# is what makes the rest of the allowlist honest, and it is also what puts the
# one path a draft is ever written to outside `docs/specs`.

@pytest.fixture
def checkout(tmp_path):
    root = tmp_path / "checkout"
    (root / "docs" / "specs").mkdir(parents=True)
    (root / "libs").mkdir()
    worktree = tmp_path / "spec-drafts"
    worktree.mkdir()
    (root / "docs" / "specs" / "drafts").symlink_to(worktree)
    return root


def elsewhere(tmp_path):
    """Env that moves the temp allowlist out of the way.

    pytest's tmp_path lives *under* the system temp directory, which the guard allows
    outright -- so without this every assertion below would pass for the wrong reason.
    """
    scratch = tmp_path / "scratch"
    scratch.mkdir(exist_ok=True)
    return {"TMPDIR": str(scratch), "TMP": str(scratch), "TEMP": str(scratch)}


def test_a_draft_is_written_through_a_link_that_leaves_the_checkout(checkout, tmp_path):
    for path in ("docs/specs/drafts/new_problem.md",
                 str(checkout / "docs" / "specs" / "drafts" / "new_problem.md")):
        assert verdict("Write", {"file_path": path}, root=str(checkout),
                       **elsewhere(tmp_path)) == ALLOWED


def test_a_draft_is_still_markdown(checkout, tmp_path):
    assert verdict("Write", {"file_path": "docs/specs/drafts/notes.txt"}, root=str(checkout),
                   **elsewhere(tmp_path)) == BLOCKED


def test_the_link_opens_what_it_points_at_and_not_its_neighbours(checkout, tmp_path):
    """The drafts worktree sits at the workspace root, beside the sibling checkouts this
    allowlist exists to protect. Allowing its parent would hand over all of them."""
    (tmp_path / "sibling").mkdir()
    for path in (tmp_path / "sibling" / "x.md", tmp_path / "x.md"):
        assert verdict("Write", {"file_path": str(path)}, root=str(checkout),
                       **elsewhere(tmp_path)) == BLOCKED


def test_a_checkout_with_no_drafts_link_is_unchanged(tmp_path):
    """CI, a fresh clone, and any checkout whose `git clean -xfd` took the symlink. The
    second spec root collapses onto the first and nothing new is allowed."""
    root = tmp_path / "bare"
    (root / "docs" / "specs").mkdir(parents=True)
    assert verdict("Write", {"file_path": "docs/specs/x.md"}, root=str(root),
                   **elsewhere(tmp_path)) == ALLOWED
    assert verdict("Write", {"file_path": "libs/x.cpp"}, root=str(root),
                   **elsewhere(tmp_path)) == BLOCKED


# --- the shell --------------------------------------------------------------
#
# Every entry below was a live bypass of an earlier guard that tried to read the
# command line and refuse the writes in it. They are here as a record of why
# there is no shell rather than as a list of things to keep refusing: the rule
# that refuses them is the same one that refuses `git log`, and it has no next
# spelling to be caught out by.

@pytest.mark.parametrize("command", [
    # a write, plainly
    "git commit -m x",
    "rm -rf build",
    "sed -i 's/a/b/' libs/bgl_extended/src/x.cpp",
    # a write behind a read's flag
    "find . -delete",
    r"find . -exec rm -rf {} \;",
    "sort -o CLAUDE.md CLAUDE.md",
    "uniq CLAUDE.md /tmp/out.txt",
    "git diff --output=CLAUDE.md",
    "git grep --open-files-in-pager='touch PWNED; true' Renderer",
    # a write behind a spelling
    "git branch list",                              # creates a branch called `list`
    "git reflog expire --all",
    r"git comm\it -m x",                            # the shell strips the backslash
    "SED -i s/a/b/ libs/x.cpp",                     # macOS resolves case-insensitively
    "ws FEATURE thing",
    "git -C ../ask commit -m x",                    # aimed at a sibling checkout
    "git --{git-dir=../../bernini/.git,z} log",     # ...via brace expansion
    "git diff --{output=a.txt,z} HEAD~1 HEAD",
    # a write behind composition
    "echo x>libs/bgl_extended/src/x.h",
    'echo x > "libs/bgl_extended/src/x.h"',
    'sh -c "echo x > libs/bgl_extended/src/x.h"',
    "R=$(git commit -m x)",
    "echo `git commit -m x`",
    "cat <(git commit -m x)",
    "echo $(git log; git push)",
    "git status\ngit push origin HEAD",
    # ...and the reads, which go the same way
    "git log --oneline -5",
    "grep -rn Renderer libs/bgl_extended",
    "cat CLAUDE.md",
    "ls",
    "",
])
def test_there_is_no_shell(command):
    assert verdict("Bash", {"command": command}) == BLOCKED


def test_powershell_is_a_shell_too():
    assert verdict("PowerShell", {"command": "Get-ChildItem"}) == BLOCKED


# --- failing closed ---------------------------------------------------------

def raw(payload_text):
    env = dict(os.environ, CLAUDE_PROJECT_DIR=ROOT, WS_ASK="1")
    return subprocess.run([sys.executable, HOOK], input=payload_text, env=env,
                          capture_output=True, text=True).returncode


@pytest.mark.parametrize("payload", [
    '{"tool_name":"Write","tool_input":{"file_path":42}}',
    '{"tool_name":"Write","tool_input":{"file_path":{"a":"docs/specs/x.md"}}}',
    '{"tool_name":"NotebookEdit","tool_input":{"notebook_path":["x"]}}',
    '{"tool_name":"Write","tool_input":["docs/specs/x.md"]}',
    '{"tool_name":"Write"}',
    '{"tool_name":"Write","tool_input":{"file_path":null}}',
    '{"tool_name":"Write","cwd":7,"tool_input":{"file_path":"libs/x.cpp"}}',
])
def test_a_shape_it_did_not_expect_refuses_rather_than_crashes(payload):
    """Only exit 2 blocks a tool call; every other status is an error the harness reports
    while letting the call through. So a traceback here is a write allowed -- the one
    outcome nothing else in the guard permits."""
    assert raw(payload) == BLOCKED
