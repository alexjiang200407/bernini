"""No CMake file in the engine may name CMAKE_SOURCE_DIR.

The engine is built as a subdirectory of a game (docs/embedding.md), where CMAKE_SOURCE_DIR is
the *game's* root and every path built from it points at a file that is not there. BERNINI_ROOT
is this checkout either way.

This is the only thing that can catch a reintroduction. Every build in the repository is
top-level, so a fresh `${CMAKE_SOURCE_DIR}/...` is correct by every other measure -- and
`just embed` only compiles what a nested configure reaches, which is neither apps/editor,
examples/, nor anything behind BUILD_TESTS.
"""

import os
import re

import util.cmake_tools as ct

# Trees that are not ours to hold to this: build output, and whatever vcpkg unpacked into it.
SKIP_DIRS = {"build", ".git", "external", "vcpkg_installed", "dist"}

BANNED = re.compile(r"\bCMAKE_SOURCE_DIR\b")


def cmake_files():
    for dirpath, dirnames, filenames in os.walk(ct.REPO_ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if name.lower() == "cmakelists.txt" or name.endswith(".cmake"):
                yield os.path.join(dirpath, name)


def code(line):
    """`line` with its trailing comment removed -- the rule is about use, not about mentions."""
    return line.split("#", 1)[0]


def test_the_engine_names_its_own_root():
    offenders = []
    for path in cmake_files():
        with open(path, encoding="utf-8", errors="replace") as fh:
            for number, line in enumerate(fh, 1):
                if BANNED.search(code(line)):
                    offenders.append(f"{os.path.relpath(path, ct.REPO_ROOT)}:{number}: {line.strip()}")

    assert not offenders, (
        "CMAKE_SOURCE_DIR is the top of the *build*, which under a game's add_subdirectory is the "
        "game. Use BERNINI_ROOT:\n  " + "\n  ".join(offenders)
    )


def test_the_root_is_defined_before_it_is_used():
    """A guard that only forbids the wrong name would pass on an empty variable."""
    with open(os.path.join(ct.REPO_ROOT, "CMakeLists.txt"), encoding="utf-8") as fh:
        text = fh.read()

    defined = text.index('set(BERNINI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")')
    assert defined < text.index("${BERNINI_ROOT}")


def test_the_scan_reaches_the_files_it_claims_to():
    """A walk that matched nothing would pass the first case for the wrong reason."""
    found = {os.path.relpath(p, ct.REPO_ROOT) for p in cmake_files()}

    for expected in ("CMakeLists.txt",
                     os.path.join("cmake", "enable_compiler_cache.cmake"),
                     os.path.join("libs", "core", "CMakeLists.txt"),
                     os.path.join("libs", "bgl_common", "idl", "CMakelists.txt"),
                     os.path.join("apps", "editor", "CMakeLists.txt"),
                     os.path.join("examples", "util", "CMakeLists.txt")):
        assert expected in found
