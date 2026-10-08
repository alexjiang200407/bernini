"""`build.py -D NAME=VALUE`: a cache variable on the configure's command line, never the environment.

CI turns BERNINI_MSVC_COMPILER_CACHE on this way, so a define that did not reach the configure would
leave CI compiling without its cache and nothing failing.
"""

import os
import subprocess
import sys

import pytest

import build
import util.cmake_tools as ct


def test_a_define_becomes_a_cmake_argument():
    assert build.define_args(["BERNINI_MSVC_COMPILER_CACHE=ON", "A=b=c", "EMPTY="]) == [
        "-DBERNINI_MSVC_COMPILER_CACHE=ON", "-DA=b=c", "-DEMPTY="]
    assert build.define_args(None) == []


@pytest.mark.parametrize("bad", ["BERNINI_MSVC_COMPILER_CACHE", "=ON"])
def test_a_define_without_a_name_and_a_value_is_refused(bad):
    with pytest.raises(SystemExit, match="NAME=VALUE"):
        build.define_args([bad])


def dry_run(*args):
    return subprocess.run([sys.executable, os.path.join(ct.REPO_ROOT, "scripts", "build.py"),
                           "--dry-run", *args], capture_output=True, text=True, cwd=ct.REPO_ROOT)


def test_a_define_reaches_the_configure_even_in_a_configured_dir():
    done = dry_run("-D", "BERNINI_PROFILING=ON")
    if "cmake not found" in done.stderr:
        pytest.skip("no cmake on this machine")
    assert done.returncode == 0, done.stderr
    configure = next(line for line in done.stdout.splitlines() if line.startswith("configure:"))
    assert "-DBERNINI_PROFILING=ON" in configure
    assert "skipped" not in configure


def test_a_define_with_no_configure_is_refused():
    done = dry_run("-D", "BERNINI_PROFILING=ON", "--no-configure")
    assert done.returncode != 0
    assert "--no-configure" in done.stderr
