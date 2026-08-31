"""The job budget several checkouts' builds share.

Every case drives a pool under `tmp_path` and never the machine's own in `jobserver.STATE_DIR`.
That matters the same way it does for the suite lock: a test on the real pool would be drawing
tokens from the builds actually running beside it, and proving something about a pool it does not
own.

The contention is real rather than simulated. `sleep` stands in for a compiler, and the claim under
test is a wall-clock one -- that two consumers together do not exceed the budget -- so it is
measured on two processes actually competing for it.
"""

import os
import subprocess
import sys
import time

import pytest

import util.jobserver as jobserver

# One "compile". Long enough that the difference between running in one round and running in three
# is unmistakable, short enough to keep the suite a few seconds.
JOB = 0.4


def ninja():
    """The ninja to drive these cases with, or None when there is none to drive."""
    import util.config as cfg
    found = cfg.find_ninja()
    if not found:
        return None
    # Jobserver *client* support arrives in ninja 1.13; an older one ignores MAKEFLAGS entirely and
    # would fail these cases for a reason that is not a defect.
    out = subprocess.run([found, "--version"], capture_output=True, text=True).stdout.strip()
    major, _, minor = out.partition(".")
    try:
        if (int(major), int(minor.split(".")[0])) < (1, 13):
            return None
    except ValueError:
        return None
    return found


needs_ninja = pytest.mark.skipif(ninja() is None,
                                 reason="needs ninja >= 1.13 for jobserver client support")


def write_project(directory, jobs):
    """A ninja project of `jobs` edges that each sleep for JOB seconds."""
    os.makedirs(directory, exist_ok=True)
    lines = ["rule slow", f"  command = sleep {JOB} && touch $out", ""]
    lines += [f"build f{i}: slow" for i in range(jobs)]
    with open(os.path.join(directory, "build.ninja"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return directory


def run_all(projects, env):
    """Run every project at once and return how long the slowest took."""
    started = time.time()
    running = [subprocess.Popen([ninja(), "-C", p], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
               for p in projects]
    for process in running:
        assert process.wait() == 0
    return time.time() - started


@needs_ninja
def test_two_builds_share_one_budget(tmp_path):
    """The whole point: N builds together stay inside the budget, rather than each taking it.

    Uncapped, both projects run every edge at once and finish in about one job's time. Sharing two
    tokens, the eight edges between them cannot run more than a few at a time, so it takes several
    rounds -- and the assertion is on that ratio rather than on a wall-clock ceiling, so it holds on
    a loaded machine and in a debug build.
    """
    projects = [write_project(str(tmp_path / "a"), 4), write_project(str(tmp_path / "b"), 4)]

    uncapped = run_all(projects, dict(os.environ))

    for project in projects:
        for name in os.listdir(project):
            if name.startswith("f"):
                os.unlink(os.path.join(project, name))

    with jobserver.shared_budget(dict(os.environ), tokens=2,
                                 state_dir=str(tmp_path / "state")) as env:
        capped = run_all(projects, env)

    assert capped > uncapped * 1.5


@needs_ninja
def test_the_budget_caps_a_single_build(tmp_path):
    """A lone build is capped too -- otherwise the first one to start would take the machine."""
    project = write_project(str(tmp_path / "solo"), 4)

    with jobserver.shared_budget(dict(os.environ), tokens=1,
                                 state_dir=str(tmp_path / "state")) as env:
        elapsed = run_all([project], env)

    # Four edges, at most one token plus ninja's implicit one: at least two rounds, never one.
    assert elapsed > JOB * 1.8


def test_disabled_hands_back_the_environment_untouched(tmp_path):
    env = {"PATH": "/usr/bin"}

    with jobserver.shared_budget(env, enabled=False, state_dir=str(tmp_path)) as result:
        assert result == env
        assert "MAKEFLAGS" not in result


def test_an_unusable_state_dir_fails_open(tmp_path, capsys):
    """A pool that cannot be reached must not stop the build, and must not silently pretend."""
    blocker = tmp_path / "not-a-directory"
    blocker.write_text("", encoding="utf-8")
    env = {"PATH": "/usr/bin"}

    with jobserver.shared_budget(env, state_dir=str(blocker), out=sys.stderr) as result:
        assert "MAKEFLAGS" not in result

    assert "uncapped" in capsys.readouterr().err


def test_existing_makeflags_are_kept(tmp_path):
    """A build launched from a make that is itself a jobserver must not lose its own flags."""
    env = {"MAKEFLAGS": "--no-print-directory"}

    with jobserver.shared_budget(env, tokens=2, state_dir=str(tmp_path / "state")) as result:
        assert result["MAKEFLAGS"].startswith("--no-print-directory ")
        assert "--jobserver-auth=" in result["MAKEFLAGS"]


@pytest.mark.skipif(sys.platform == "win32", reason="the fifo protocol is POSIX-only")
def test_a_second_build_joins_the_pool_rather_than_refilling_it(tmp_path):
    """Two nested budgets are one pool of N, not two of N -- otherwise the cap doubles per build."""
    state = str(tmp_path / "state")

    with jobserver.shared_budget(dict(os.environ), tokens=3, state_dir=state):
        with jobserver.shared_budget(dict(os.environ), tokens=3, state_dir=state):
            fifo = jobserver.paths_in(state).fifo
            handle = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
            try:
                drained = os.read(handle, 64)
            finally:
                os.close(handle)

    assert len(drained) == 3


@pytest.mark.parametrize("tokens", [0, -1])
def test_a_budget_below_one_is_refused_rather_than_defaulted(tmp_path, capsys, tokens):
    """0 is falsy, so the obvious `tokens or default` silently ignores an explicit --jobs 0, and a
    negative one primes an empty pool that nothing can draw from."""
    env = {"PATH": "/usr/bin"}

    with jobserver.shared_budget(env, tokens=tokens, state_dir=str(tmp_path / "state")) as result:
        assert "MAKEFLAGS" not in result

    assert "uncapped" in capsys.readouterr().err
