"""When a search for a C++ symbol is sent to the language server instead of to grep.

`.claude/hooks/lsp_nudge.py` refuses a search that reaches the C++ sources and leaves
`scripts/bgrep` alone. What these pin is the boundary. A hook that refuses a legitimate
search is a hook that gets switched off, so the cases below are weighted towards what it
must leave alone: CMakeLists.txt, docs, JSON, `.bmaterial`, `.slang`, and anything under
`scripts/`.

The hook is run as a subprocess rather than imported, because that is how Claude Code
runs it: a payload on stdin and a refusal as exit 2 with stderr.
"""

import json
import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOOK = os.path.join(ROOT, ".claude", "hooks", "lsp_nudge.py")

REFUSED = "refused"
SILENT = "silent"


def run(command):
    """(tier, message) for `command`."""
    payload = json.dumps({"tool_name": "Bash", "tool_input": {"command": command}})
    done = subprocess.run(
        [sys.executable, HOOK],
        input=payload,
        capture_output=True,
        text=True,
        check=False,
    )

    if done.returncode == 2:
        return REFUSED, done.stderr
    assert done.returncode == 0, done.stderr
    # Nothing is rewritten and nothing is advised: the hook either refuses or says nothing.
    assert not done.stdout.strip(), done.stdout
    return SILENT, ""


def tier(command):
    return run(command)[0]


# A bare identifier aimed at the C++ sources. Refused rather than advised, because advice
# arrives beside the results it was meant to prevent and is read after them.
@pytest.mark.parametrize(
    "command",
    [
        'grep -rn "AcceptsMaterial" libs',
        "grep -rn AcceptsMaterial libs apps",
        "rg GameSlotRow libs/bgl_extended",
        "grep -n SurfaceDescOf apps/editor/src/Windows/MaterialEditor/graph_compiler.cpp",
        "grep -rn shadingModel libs/assetlib/src/bmaterial_io.cpp",
        "grep -rn HasSkinBinding libs/ apps/",
    ],
)
def test_a_cpp_symbol_search_is_refused(command):
    said, message = run(command)
    assert said == REFUSED
    assert "findReferences" in message


# `A\|B\|C` over the sources is one findReferences question asked of three names -- the
# most natural way to ask it, and invisible to a test that only accepts a bare identifier.
# It went unnoticed because the command was split on `|` before it was tokenised, which
# tore the pattern in half.
@pytest.mark.parametrize(
    "command",
    [
        r'grep -rn "toMatrix\|formatSize\|findAttribute" libs apps examples',
        'rg "toMatrix|formatSize" libs',
    ],
)
def test_an_alternation_of_symbols_is_one_question(command):
    said, message = run(command)
    assert said == REFUSED
    assert "toMatrix" in message


# No path at all reaches the C++ sources from anywhere in this repo, so it is the same
# question as naming one.
@pytest.mark.parametrize(
    "command",
    [
        'grep -rn "CompilePreviewMaterial"',
        "grep -rn GameSlotRow",
        "grep -Rn GameSlotRow",
        "grep -nr GameSlotRow",
        "grep --recursive GameSlotRow",
        # rg walks the working directory with no flag at all, so it needs none to reach them.
        "rg GameSlotRow",
    ],
)
def test_a_search_with_no_path_reaches_the_sources(command):
    said, message = run(command)
    assert said == REFUSED
    assert "findReferences" in message


# The tier that matters most. grep and sed keep every job that is not a C++ symbol, and a
# build file is the case that would make this hook hated.
@pytest.mark.parametrize(
    "command",
    [
        # Build files, whatever directory they sit in.
        "grep -rn BERNINI_ROOT CMakeLists.txt",
        "grep -rn BERNINI_ROOT libs/assetlib/CMakeLists.txt",
        "grep -rn enable_coverage cmake/enable_coverage.cmake",
        "sed -i '' 's/STATIC/SHARED/' libs/assetlib_structs/CMakeLists.txt",
        # Not C++, whatever directory it sits in: Slang has no server here, and the rest
        # are prose, data and build files.
        "grep -rn cGameSlotRows libs/bgl_common/shaders/src/idl/PsoType.slang",
        'grep -rn "surface" docs/game_defined_surfaces.md',
        "grep -n shadingModel Data/Authored/Materials/Dog/Dog_Rim.bmaterial",
        "grep -rn skeletonSignature scripts/tests/test_draft_commit.py",
        # A regex is asking something the LSP cannot answer at all.
        'grep -rn "TEST_CASE.*surface" libs',
        'grep -rn "^struct" libs',
        # Filtering another command's output rather than searching the tree.
        "ls libs | grep bglfoo",
        "git status --short | grep bmaterial",
        # Too short to be worth a word: a two-letter search is not a symbol lookup.
        "grep -rn foo libs",
        # Reading a C++ file is not searching it.
        "sed -n '1,30p' libs/assetlib/src/skeleton.cpp",
        # Nothing to do with searching.
        "just build editor",
    ],
)
def test_everything_else_is_left_alone(command):
    assert tier(command) == SILENT


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
    said, message = run(command)
    assert said == REFUSED
    assert "GameSlotRow" in message
    for value in ("'3'", "'2'", "'1'", "'cpp'", "'always'"):
        assert value not in message


def test_the_symbol_searched_for_is_named_back():
    # workspaceSymbol takes the name, so the refusal is only actionable if it carries one.
    assert "GameSlotRow" in run("grep -rn GameSlotRow libs")[1]


# Completeness is the one C++ case grep still owns -- clangd's index lags the working tree,
# so a rename or a last-call-site check wants the regex. Asking for it costs a flag, which is
# the difference between a decision and advice skimmed past.
@pytest.mark.parametrize(
    "command",
    [
        "scripts/bgrep -rn HasSkinBinding libs/ apps/",
        "scripts/bgrep -rn toMatrix libs apps examples",
        "./scripts/bgrep -rn hash_string libs",
        "bgrep -rn SkinMatrix libs/bgl_common",
    ],
)
def test_a_stated_sweep_is_allowed(command):
    assert tier(command) == SILENT


# bgrep licenses only the invocation it names. One in a chain does not excuse a bare grep
# beside it -- the point of a program rather than a comment on the line.
def test_a_bgrep_elsewhere_does_not_excuse_a_grep():
    assert tier("scripts/bgrep -rn Foo docs && grep -rn GameSlotRow libs/core") == REFUSED


# A --force that survived from the earlier design is not a way through any more; it is just
# a flag grep will reject.
def test_the_old_force_flag_is_not_an_override():
    assert tier("grep --force -rn GameSlotRow libs") == REFUSED


# A sweep into a tree holding C++ is refused whatever the pattern looks like. The shape of a
# name cannot say which language it is: `hash_string` is C++ (STYLE.md gives core/ a
# lower_case domain) spelled exactly as the CMake function `enable_coverage` is. A rule read
# off the name exempts core/ or refuses CMake, and there is no third answer -- so the caller
# off the name exempts core/ or refuses CMake, so the caller reaches for bgrep instead.
@pytest.mark.parametrize(
    "command",
    [
        "grep -rn HasSkinBinding libs/",
        "grep -rn toMatrix libs apps examples",
        "grep -rn hash_string libs",
        "grep -rn div_ceil libs/core",
        "grep -rn ENABLE_COVERAGE libs/",
        "grep -rn enable_coverage libs apps",
    ],
)
def test_a_sweep_into_a_cpp_tree_is_refused_whatever_the_pattern(command):
    assert tier(command) == REFUSED


# ...and bgrep carries every one of them through, which is what keeps the rule above
# affordable: a CMake name swept across a subsystem costs a prefix, not an argument.
@pytest.mark.parametrize(
    "command",
    [
        "scripts/bgrep -rn ENABLE_COVERAGE libs/",
        "scripts/bgrep -rn hash_string libs",
        "scripts/bgrep -rn SkinMatrix libs/bgl_common",
    ],
)
def test_the_flag_carries_a_non_cpp_sweep_through(command):
    assert tier(command) == SILENT


# A flag confining the search to C++ says the subject is C++ whatever the pattern looks like.
@pytest.mark.parametrize(
    "command",
    [
        "rg -t cpp ENABLE_COVERAGE libs",
        "grep -rn --include=*.cpp ENABLE_COVERAGE libs",
    ],
)
def test_a_cpp_only_flag_settles_it(command):
    assert tier(command) == REFUSED


# What follows a redirect is a file being written, not a path being searched. Unrecognised, a
# `> dump.cpp` turns a grep over prose into a refused C++ search.
@pytest.mark.parametrize(
    "command",
    [
        "grep -n SomeCppWord docs/notes.txt > /tmp/dump.cpp",
        "grep -rn ENABLE_COVERAGE CMakeLists.txt >> /tmp/out.h",
        "grep -n Something docs/skinning.md 2> /dev/null",
    ],
)
def test_a_redirect_target_is_not_a_search_path(command):
    assert tier(command) != REFUSED


# shlex splits `2>&1` into `2`, `>&`, `1`, so the operator ends in `&` rather than in the
# angle bracket. Missed, its three tokens survive as bogus paths and silence a search that
# should have been answered -- the same class of bug as a swallowed redirect target, via the
# one spelling the character class forgot. It is also the commonest idiom in the shell.
@pytest.mark.parametrize(
    "command",
    [
        "grep -rn GameSlotRow 2>&1",
        "grep -rn GameSlotRow libs 2>&1",
        "grep -rn GameSlotRow libs/bgl/include/bgl/IScene.h 2>&1",
    ],
)
def test_merging_stderr_is_not_a_search_path(command):
    # Whatever tier it lands in, it must be the one it would reach without the redirect.
    assert tier(command) == tier(command.replace(" 2>&1", ""))


# `2>out` is a descriptor and `2 > out` is an argument named `2` beside a redirect. shlex
# drops the space that tells them apart, so the descriptor is only taken off the front of an
# operator it was actually written against.
def test_a_spaced_digit_is_an_argument_not_a_descriptor():
    assert tier("grep -rn MyIdentifier 2 > out.txt") == tier("grep -rn MyIdentifier 2")


# Shaders are humped PascalCase too -- `SkinMatrix`, `TableFrameSlots` in
# libs/bgl_common/shaders/src/lib/anim/skinning.slang -- and Slang has no language server
# here, so refusing one would answer it with tools that cannot reach it. CLAUDE.md promises
# a .slang identifier stays grep's, and a shader tree is where they live.
@pytest.mark.parametrize(
    "command",
    [
        "grep -rn SkinMatrix libs/bgl_common/shaders/src",
        "grep -rn TableFrameSlots libs/bgl_extended/shaders",
        "grep -rn SkinMatrix libs/bgl_common/shaders/src/lib/anim/skinning.slang",
    ],
)
def test_a_shader_tree_is_not_the_lsps_to_answer(command):
    assert tier(command) != REFUSED


# Where a subsystem holds both, the hook cannot tell a Slang name from a C++ one -- so the
# refusal has to offer a way out that actually works rather than only naming the LSP.
def test_the_refusal_names_the_slang_escape():
    said, message = run("grep -rn SkinMatrix libs/bgl_common")
    assert said == REFUSED
    assert "Slang" in message
    assert "bgrep" in message


# `grep -rn Symbol .` from the repo root is the most natural spelling of "search everywhere",
# and it named no path the classifier recognised, so it once fell through every tier in silence.
@pytest.mark.parametrize("command", ["grep -rn GameSlotRow .", "grep -rn GameSlotRow ./"])
def test_a_dot_path_is_the_whole_tree(command):
    said, message = run(command)
    assert said == REFUSED
    assert "findReferences" in message


# Editing C++ with a regex nothing checks: a pattern matching twice edits twice, and one
# matching nothing succeeds silently. Edit fails loudly instead.
@pytest.mark.parametrize(
    "command",
    [
        "sed -i '' 's/toMatrix/toMatrix2/' libs/assetlib_structs/src/transform.cpp",
        "sed -i.bak 's/a/b/' libs/bgl/include/bgl/IScene.h",
        "sed --in-place 's/a/b/' apps/editor/src/MainWindow.cpp",
    ],
)
def test_editing_a_cpp_source_with_sed_is_refused(command):
    said, message = run(command)
    assert said == REFUSED
    assert "Edit" in message


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
