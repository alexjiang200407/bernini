"""The machine-wide suite lock: that a second suite waits, and that a dead one lets go.

Every test locks a path of its own under `tmp_path` and never the real one in
`lock.LOCK_PATH`. That matters twice over: this suite runs from `just test`, so a test that
took the real lock would block behind the suites it is being run alongside -- and it would
be holding the machine's lock while proving something about a different one.

The contention is real rather than simulated: each case launches competitor processes that
take the lock, write when they entered and left, and hold for HOLD seconds. Serialisation is
then a claim about those intervals, which is the thing a caller actually depends on.
"""

import os
import subprocess
import sys
import time

import pytest

import util.lock as lock

SCRIPTS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Long enough that two competitors launched back to back overlap comfortably when nothing
# stops them, short enough that the suite stays a few seconds.
HOLD = 1.0

COMPETITOR = """
import sys, time
sys.path.insert(0, {scripts!r})
import util.lock as lock

out = open({out!r}, "w")
with lock.suite_lock(enabled={enabled!r}, path={path!r}, out=out):
    with open({log!r}, "a") as f:
        f.write("enter %.6f\\n" % time.time())
        f.flush()
    time.sleep({hold!r})
    with open({log!r}, "a") as f:
        f.write("exit %.6f\\n" % time.time())
out.close()
"""


def competitor(tmp_path, name, enabled=True, hold=HOLD):
    """A process that takes the lock under `tmp_path`, holds it, and records both moments."""
    log = tmp_path / f"{name}.log"
    out = tmp_path / f"{name}.out"
    source = COMPETITOR.format(scripts=SCRIPTS, out=str(out), enabled=enabled,
                               path=str(tmp_path / "suite.lock"), log=str(log), hold=hold)
    proc = subprocess.Popen([sys.executable, "-c", source])
    return proc, log, out


def interval(log):
    """The (entered, left) pair a competitor wrote, as wall-clock seconds."""
    stamps = dict(line.split() for line in log.read_text().splitlines())
    return float(stamps["enter"]), float(stamps["exit"])


def wait_for(proc, log):
    assert proc.wait(timeout=120) == 0
    return interval(log)


def held_when(log, deadline=30.0):
    """Block until the competitor writing `log` has entered its critical section."""
    limit = time.monotonic() + deadline
    while time.monotonic() < limit:
        if log.exists() and "enter" in log.read_text():
            return True
        time.sleep(0.02)
    return False


def test_a_second_suite_waits_for_the_first(tmp_path):
    first, first_log, _ = competitor(tmp_path, "first")
    second, second_log, _ = competitor(tmp_path, "second")

    runs = sorted([wait_for(first, first_log), wait_for(second, second_log)])
    assert runs[0][1] <= runs[1][0], "the second suite ran while the first still held the lock"


def test_they_overlap_when_the_lock_is_off(tmp_path):
    """The control for the case above: without the lock the two runs do collide.

    Without this, a bug that never took the lock at all -- or a HOLD too short for the
    processes to meet -- would leave the serialisation test passing on an empty claim.
    """
    first, first_log, _ = competitor(tmp_path, "first", enabled=False)
    second, second_log, _ = competitor(tmp_path, "second", enabled=False)

    runs = sorted([wait_for(first, first_log), wait_for(second, second_log)])
    assert runs[1][0] < runs[0][1], "the two unlocked runs never overlapped"


def test_a_killed_holder_releases_the_lock(tmp_path):
    """An agent killed mid-suite must not wedge the machine.

    This is why the lock is an advisory lock on an open file rather than a pid written into
    one: nothing runs on the holder's way out, so nothing has to.
    """
    holder, log, _ = competitor(tmp_path, "holder", hold=600)
    assert held_when(log)

    holder.kill()
    holder.wait(timeout=30)

    started = time.monotonic()
    with lock.suite_lock(path=str(tmp_path / "suite.lock")):
        assert time.monotonic() - started < 5.0


def test_the_waiter_names_who_holds_it(tmp_path):
    """A run that goes quiet has to say what it is waiting for, and which checkout has it."""
    holder, holder_log, _ = competitor(tmp_path, "holder", hold=3)
    assert held_when(holder_log)

    waiter, waiter_log, waiter_out = competitor(tmp_path, "waiter", hold=0)
    wait_for(waiter, waiter_log)
    holder.wait(timeout=30)

    reported = waiter_out.read_text()
    assert "waiting for the test suite lock" in reported
    assert f"(pid {holder.pid})" in reported


def test_the_holder_record_survives_a_longer_predecessor(tmp_path):
    """A short label must not leave the previous holder's tail behind it.

    The record is a fixed RECORD bytes, padded rather than truncated, because truncating a
    file whose locked byte sits past the record is not something Windows guarantees.
    """
    path = str(tmp_path / "suite.lock")
    with lock.suite_lock(path=path):
        pass

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "r+b") as handle:
        handle.write(f"{time.time():.0f}\tan-extremely-long-checkout-name (pid 1)".encode()
                     .ljust(lock.RECORD))

    with lock.suite_lock(path=path):
        label, age = lock.read_holder(path)

    assert label == lock.describe()
    assert age < 5.0


@pytest.mark.parametrize("target, expected", [
    ("bgl_extended_tests", True),
    ("editor_tests", True),
    ("editor", False),
    ("assetlib_cli", False),
])
def test_only_a_suite_runs_under_the_lock(target, expected):
    assert lock.is_suite(target) is expected


@pytest.mark.parametrize("seconds, expected", [(0, "0s"), (45, "45s"), (60, "1m00s"), (134, "2m14s")])
def test_a_wait_is_reported_in_minutes_and_seconds(seconds, expected):
    assert lock.elapsed(seconds) == expected
