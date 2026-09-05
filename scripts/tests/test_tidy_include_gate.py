"""That a missing include fails the pre-commit hook, and only where it was switched on.

The gate is one word in `scripts/tidy.py`'s --warnings-as-errors and one line in a
subsystem's .clang-tidy, and neither shows up in any other test: a check that is reported but
does not move the exit code looks identical in the output and lets the hook wave the
violation through.

So these drive `--changed`, which is what `.githooks/pre-commit` actually runs -- not the
whole-file check. The difference is the point. `--changed` filters findings to the lines a
diff touched, and an include-cleaner finding sits at the *use* of the symbol rather than at
the top of the file, so whether a newly written `std::vector` is caught at all is a claim
about the line filter and not about the check.

Off the repo deliberately, over a git repository of its own: checking a file in the tree
would pass or fail depending on how far the sweep has got, which is a fact about the tree
rather than about the gate. `ct.REPO_ROOT` is what `changed()` resolves the diff against, so
pointing it at the fixture is what makes an isolated run possible at all -- and it means
these call `tidy.main()` in process rather than as a subprocess.
"""

import json
import os
import subprocess
import sys

import pytest

import tidy
import util.cmake_tools as ct
import util.config as cfg

CLEAN = """#include <string>

int
main()
{
\tstd::string name;
\treturn static_cast<int>(name.size());
}
"""

# The added line names std::vector, which only the PCH declares. Everything above it is
# untouched, so a finding reported here is one the line filter let through.
DIRTY = """#include <string>

int
main()
{
\tstd::string name;
\tstd::vector<std::string> names{name};
\treturn static_cast<int>(names.size());
}
"""


def git(repo, *args):
    subprocess.run(["git", "-c", "user.email=t@t", "-c", "user.name=t", *args],
                   cwd=repo, check=True, capture_output=True)


@pytest.fixture(autouse=True)
def needs_clang_tidy():
    if not cfg.find_clang_tidy():
        pytest.skip("clang-tidy is not installed")


@pytest.fixture
def repo(tmp_path, monkeypatch):
    """A committed clean source, a staged edit that needs <vector>, and a database.

    The PCH is the point rather than scaffolding: it is what makes the edit *compile*, so
    the only thing left to report is the missing direct include. Without one the source does
    not parse and a passing test would be pinning a compiler error.
    """
    (tmp_path / "pch.h").write_text("#pragma once\n#include <vector>\n#include <string>\n",
                                    encoding="utf-8")
    source = tmp_path / "main.cpp"
    source.write_text(CLEAN, encoding="utf-8")

    git(tmp_path, "init", "-q")
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-qm", "clean")

    source.write_text(DIRTY, encoding="utf-8")
    git(tmp_path, "add", "main.cpp")

    command = f'clang++ -std=gnu++20 -include "{tmp_path / "pch.h"}" -o main.o -c "{source}"'
    sdk = os.environ.get("SDKROOT")
    if sys.platform == "darwin" and not sdk:
        probe = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True)
        sdk = probe.stdout.strip() if probe.returncode == 0 else ""
    if sdk:
        command += f' -isysroot "{sdk}"'

    build = tmp_path / "build"
    build.mkdir()
    (build / "compile_commands.json").write_text(json.dumps(
        [{"directory": str(build), "command": command, "file": str(source)}]), encoding="utf-8")

    monkeypatch.setattr(ct, "REPO_ROOT", str(tmp_path))
    return tmp_path, build


def run_changed(build, monkeypatch):
    monkeypatch.setattr(sys, "argv", ["tidy.py", "--build-dir", str(build), "--changed"])
    return tidy.main()


def run_whole_file(build, source, monkeypatch):
    monkeypatch.setattr(sys, "argv", ["tidy.py", "--build-dir", str(build), str(source)])
    return tidy.main()


def enable(directory, checks):
    (directory / ".clang-tidy").write_text(
        f"Checks: '-*,{checks}'\nHeaderFilterRegex: ''\n", encoding="utf-8")


def test_a_newly_written_missing_include_fails_the_hook(repo, monkeypatch, capsys):
    """The line the diff added names std::vector and does not include <vector>."""
    root, build = repo
    enable(root, "misc-include-cleaner")

    rc = run_changed(build, monkeypatch)
    assert 'no header providing "std::vector"' in capsys.readouterr().out
    assert rc != 0


def test_a_subsystem_that_has_not_switched_it_on_is_unaffected(repo, monkeypatch, capsys):
    """The same staged edit, judged by a config running only the naming check."""
    root, build = repo
    enable(root, "readability-identifier-naming")

    rc = run_changed(build, monkeypatch)
    assert "misc-include-cleaner" not in capsys.readouterr().out
    assert rc == 0


def test_an_untouched_violation_is_not_the_next_editors_problem(repo, monkeypatch, capsys):
    """`std::string` has been missing <string>'s neighbours since the first commit.

    The staged edit adds one line. Nothing on the lines it did not touch may be reported, or
    the hook fails whoever happens to open a file next -- which is the reason `--changed`
    filters at all.

    Checked twice, because silence proves nothing on its own: the same file checked whole
    must report the very finding the filtered run swallowed, or this passes for the one
    reason that would make it worthless -- the check never ran.
    """
    root, build = repo
    enable(root, "misc-include-cleaner")
    source = root / "main.cpp"
    # A pre-existing unused include, committed, and untouched by the staged edit.
    committed = CLEAN.replace("#include <string>", "#include <string>\n#include <deque>")
    source.write_text(committed, encoding="utf-8")
    git(root, "add", "main.cpp")
    git(root, "commit", "-qm", "an unused include, already there")
    source.write_text(committed.replace("\treturn", "\tname.clear();\n\treturn"),
                      encoding="utf-8")
    git(root, "add", "main.cpp")

    assert run_changed(build, monkeypatch) == 0
    assert "deque" not in capsys.readouterr().out

    assert run_whole_file(build, source, monkeypatch) != 0
    assert "deque" in capsys.readouterr().out
