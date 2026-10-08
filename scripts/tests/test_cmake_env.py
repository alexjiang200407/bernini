"""No CMake file or preset in the engine may read the environment but VCPKG_ROOT.

An environment variable is per-shell state nothing on disk records: a build that configures in the
shell that exported it configures differently, or not at all, from an IDE, a plain terminal or a
copy of the workspace. VCPKG_ROOT is the one exception -- the machine's vcpkg checkout, which the
presets' toolchain file resolves through. Everything else comes from the layout, a file, or -D.

Only direct reads are pinned: `$ENV{X}` and `DEFINED ENV{X}` in CMake, `$env{X}` in a preset.
"""

import json
import os
import re

import util.cmake_tools as ct
from test_cmake_root import cmake_files, code

ALLOWED = {"VCPKG_ROOT"}

CMAKE_READ = re.compile(r"(?<!\w)\$?ENV\{(\w+)\}")
PRESET_READ = re.compile(r"\$env\{(\w+)\}")


def offenders(lines, pattern):
    for number, line in lines:
        for name in pattern.findall(line):
            if name not in ALLOWED:
                yield number, name, line.strip()


def test_cmake_reads_no_environment():
    found = []
    for path in cmake_files():
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = [(n, code(line)) for n, line in enumerate(fh, 1)]
        found += [f"{os.path.relpath(path, ct.REPO_ROOT)}:{n}: {line}"
                  for n, _, line in offenders(lines, CMAKE_READ)]

    assert not found, ("a CMake file reads the environment; take it from the layout, a file or a "
                       "cache variable instead:\n  " + "\n  ".join(found))


def test_presets_read_no_environment_but_vcpkg_root():
    path = os.path.join(ct.REPO_ROOT, "CMakePresets.json")
    with open(path, encoding="utf-8") as fh:
        lines = list(enumerate(fh, 1))
    json.loads("".join(line for _, line in lines))

    found = [f"CMakePresets.json:{n}: {line}" for n, _, line in offenders(lines, PRESET_READ)]
    assert not found, "a preset reads the environment:\n  " + "\n  ".join(found)


def test_the_patterns_catch_what_they_claim_to():
    """A pattern that matched nothing would pass both cases for the wrong reason."""
    caught = list(offenders([(1, 'if (DEFINED ENV{WS_X})'), (2, 'set(A "$ENV{B}")'), (4, 'set(FOO_ENV{C} 1)'),
                             (3, '"toolchainFile": "$ENV{VCPKG_ROOT}/x"')], CMAKE_READ))
    assert [name for _, name, _ in caught] == ["WS_X", "B"]

    caught = list(offenders([(1, '"x": "$env{QT_DIR}"'), (2, '"t": "$env{VCPKG_ROOT}/s"')],
                            PRESET_READ))
    assert [name for _, name, _ in caught] == ["QT_DIR"]
