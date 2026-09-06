"""Which bracket an #include gets, and the three answers the rule can give.

`misc-include-cleaner` writes every insertion it makes quoted except the standard library's,
so `just tidy --fix` has to rebracket them afterwards or the sweep lands ~8,000 includes that
break STYLE.md. These pin what "afterwards" does.

The layout below is the real one in miniature -- a subsystem with a public `include/` and a
private `src/`, a second subsystem, a vendored tree, and a shared directory that is neither.
The last of those is the interesting case: the rule deliberately declines to answer, because
`examples/util` keeps its headers beside the sources that use them and the tree spells them
`<DemoWindow.h>` from elsewhere. A pass that guessed would rewrite working code.
"""

import os

import pytest

import util.include_style as st


@pytest.fixture
def tree(tmp_path):
    """A repo-shaped tree, and the search path a file in bgl_extended/src would compile with."""
    for relative in [
        "libs/bgl/include/bgl/IScene.h",
        "libs/bgl_extended/src/types/Rect.h",
        "libs/bgl_extended/src/scene/Scene.h",
        "examples/util/DemoWindow.h",
        "build/generated/bgl_common/idl/RawEntry.h",
        "build/vcpkg_installed/include/Metal/MTLBuffer.hpp",
    ]:
        path = tmp_path / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#pragma once\n", encoding="utf-8")

    dirs = [str(tmp_path / d) for d in (
        "libs/bgl_extended/src",
        "libs/bgl/include",
        "examples/util",
        "build/generated",
        "build/vcpkg_installed/include",
    )]
    return tmp_path, dirs


def restyle(tree, text, source="libs/bgl_extended/src/scene/Scene.cpp"):
    root, dirs = tree
    return st.restyle(text, str(root / source), dirs, str(root))


def test_a_published_header_is_angled(tree):
    """Another subsystem's include/ tree is its interface, whoever reaches for it."""
    fixed, changes = restyle(tree, '#include "bgl/IScene.h"\n')
    assert fixed == "#include <bgl/IScene.h>\n"
    assert changes == [('"bgl/IScene.h"', "<bgl/IScene.h>")]


def test_a_subsystems_own_src_header_is_quoted(tree):
    """The case that must not move: `types/Rect.h` is bgl_extended's own internals."""
    fixed, changes = restyle(tree, '#include "types/Rect.h"\n')
    assert fixed == '#include "types/Rect.h"\n'
    assert changes == []


def test_a_src_header_written_angled_is_corrected(tree):
    """The rule runs both ways, or `--check` would be a claim it cannot make."""
    fixed, _ = restyle(tree, "#include <types/Rect.h>\n")
    assert fixed == '#include "types/Rect.h"\n'


def test_third_party_and_generated_headers_are_angled(tree):
    """Nothing under build/ is ours -- vcpkg's tree and the generated IDL mirrors alike."""
    fixed, _ = restyle(tree, '#include "Metal/MTLBuffer.hpp"\n#include "bgl_common/idl/RawEntry.h"\n')
    assert fixed == "#include <Metal/MTLBuffer.hpp>\n#include <bgl_common/idl/RawEntry.h>\n"


def test_a_header_the_convention_does_not_reach_is_left_alone(tree):
    """`examples/util` is neither an include/ nor a src/, and both spellings are in the tree."""
    for text in ('#include "DemoWindow.h"\n', "#include <DemoWindow.h>\n"):
        fixed, changes = restyle(tree, text, source="examples/bgl_ui/src/main.cpp")
        assert fixed == text
        assert changes == []


def test_an_unresolvable_include_is_left_alone(tree):
    """A Windows-only header on macOS resolves nowhere, and is not guessed at."""
    fixed, changes = restyle(tree, '#include "win32/Registry.h"\n')
    assert fixed == '#include "win32/Registry.h"\n'
    assert changes == []


def test_a_quoted_include_finds_its_own_directory_first(tree):
    """`"Scene.h"` beside Scene.cpp resolves the way the compiler resolves it, and stays."""
    fixed, changes = restyle(tree, '#include "Scene.h"\n')
    assert fixed == '#include "Scene.h"\n'
    assert changes == []


def test_line_endings_and_trailing_comments_survive(tree):
    """The pass rewrites two characters; it must not normalise the line around them."""
    fixed, _ = restyle(tree, '#include "bgl/IScene.h"  // IWYU pragma: keep\r\n')
    assert fixed == "#include <bgl/IScene.h>  // IWYU pragma: keep\r\n"


def test_the_file_is_only_written_when_something_moved(tree):
    """restyle_file returns the changes, and leaves an already-correct file untouched."""
    root, dirs = tree
    source = root / "libs/bgl_extended/src/scene/Scene.cpp"
    source.write_text('#include "types/Rect.h"\n', encoding="utf-8")
    before = source.stat().st_mtime_ns

    entry = {"command": " ".join(f"-I{d}" for d in dirs), "directory": str(root)}
    assert st.restyle_file(str(source), entry, str(root)) == []
    assert source.stat().st_mtime_ns == before


def test_search_dirs_reads_every_flag_spelling():
    """-I is glued or spaced, -isystem takes = or a space, and -isysroot is not -isystem."""
    command = '-I/a -I /b -isystem /c -isystem=/d -iquote /e -isysroot /sdk'
    dirs = st.search_dirs(command, "/base")
    assert dirs == [os.path.normpath(p) for p in ("/a", "/b", "/c", "/d", "/e")]


def test_a_relative_search_dir_resolves_against_the_compile_directory():
    """compile_commands.json entries are relative to their own `directory`, not the cwd."""
    assert st.search_dirs("-Iinclude", "/build") == [os.path.normpath("/build/include")]
