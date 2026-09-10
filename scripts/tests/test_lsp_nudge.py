"""When a search for a C++ symbol is pointed at the language server.

`.claude/hooks/lsp_nudge.py` blocks nothing -- it is the one hook here that only
advises -- so what these pin is where it speaks and, more importantly, where it
stays quiet. A nudge on every grep would be noise, and noise is what gets a hook
switched off.

The hook is run as a subprocess rather than imported, because that is how Claude
Code runs it: a payload on stdin, and the advice on stdout as JSON.
"""

import json
import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOOK = os.path.join(ROOT, ".claude", "hooks", "lsp_nudge.py")


def advice(command):
    """The context the hook adds for `command`, or None when it says nothing."""
    payload = json.dumps({"tool_name": "Bash", "tool_input": {"command": command}})
    done = subprocess.run(
        [sys.executable, HOOK],
        input=payload,
        capture_output=True,
        text=True,
        check=False,
    )

    # Never blocks: the search runs whatever the hook thinks of it.
    assert done.returncode == 0, done.stderr
    if not done.stdout.strip():
        return None

    out = json.loads(done.stdout)
    return out["hookSpecificOutput"]["additionalContext"]


@pytest.mark.parametrize(
    "command",
    [
        'grep -rn "AcceptsMaterial" libs',
        "grep -rn AcceptsMaterial libs apps",
        "rg GameSlotRow libs/bgl_extended",
        "grep -n SurfaceDescOf apps/editor/src/Windows/MaterialEditor/graph_compiler.cpp",
        'grep -rn "CompilePreviewMaterial"',  # no path: the whole tree, recursively
        "grep -rn shadingModel libs/assetlib/src/bmaterial_io.cpp",
    ],
)
def test_a_cpp_symbol_is_pointed_at_the_lsp(command):
    said = advice(command)
    assert said is not None
    assert "findReferences" in said


# A flag's value is not the pattern. Getting this wrong is silent in both directions: the
# search that was meant to be caught goes by unremarked, or the count after -A is offered as
# a symbol to look up. `rg -t cpp` is the case that stung -- the most natural spelling of a
# C++-only search, and the one this hook exists for.
@pytest.mark.parametrize(
    "command",
    [
        "grep -A 3 GameSlotRow -r libs",
        "grep -B 2 GameSlotRow -r libs",
        "grep -m 1 GameSlotRow -r libs",
        "rg -t cpp GameSlotRow libs",
        "grep --color always -rn GameSlotRow libs",
        "grep -rn --include *.cpp GameSlotRow libs",
        # -e and -f are the other way round: their value IS the pattern.
        "grep -e GameSlotRow -r libs",
    ],
)
def test_a_flag_value_is_never_taken_for_the_pattern(command):
    said = advice(command)
    assert said is not None
    assert "GameSlotRow" in said
    for value in ("'3'", "'2'", "'1'", "'cpp'", "'always'"):
        assert value not in said


# Recursion is read off the flag characters, so every spelling of it counts rather than the
# handful somebody thought to list.
@pytest.mark.parametrize("command", ["grep -rn GameSlotRow", "grep -Rn GameSlotRow",
                                     "grep -nr GameSlotRow", "grep --recursive GameSlotRow"])
def test_every_spelling_of_recursive_is_a_tree_search(command):
    assert advice(command) is not None


def test_the_symbol_searched_for_is_named_back():
    # workspaceSymbol takes the name, so the advice is only actionable if it carries one.
    said = advice("grep -rn GameSlotRow libs")
    assert "GameSlotRow" in said


@pytest.mark.parametrize(
    "command",
    [
        # Not C++, whatever directory it sits in: Slang has no server here, and the rest
        # are prose and build files.
        "grep -rn cGameSlotRows libs/bgl_common/shaders/src/idl/PsoType.slang",
        'grep -rn "surface" docs/game_surfaces.md',
        "grep -rn BERNINI_ROOT CMakeLists.txt",
        "grep -n shadingModel Data/Authored/Materials/Dog/Dog_Rim.bmaterial",
        # A regex is asking something the LSP cannot answer at all.
        'grep -rn "TEST_CASE.*surface" libs',
        'grep -rn "^struct" libs',
        # Filtering another command's output rather than searching the tree.
        "ls libs | grep bglfoo",
        "git status --short | grep bmaterial",
        # Too short to be worth a word: a two-letter search is not a symbol lookup.
        "grep -rn foo libs",
        # Nothing to do with searching.
        "just build editor",
    ],
)
def test_everything_else_is_left_alone(command):
    assert advice(command) is None


def test_a_malformed_payload_is_not_an_error():
    # The hook runs on every Bash call, so it fails open rather than taking the session
    # down with it.
    done = subprocess.run(
        [sys.executable, HOOK],
        input="not json",
        capture_output=True,
        text=True,
        check=False,
    )
    assert done.returncode == 0
    assert not done.stdout.strip()
