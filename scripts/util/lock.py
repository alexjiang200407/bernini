#!/usr/bin/env python3
"""The machine-wide lock that lets only one test suite run at a time.

Several checkouts of this repo share one machine, and a suite is not cheap: run_tests.py
splits each one across several processes, each holding a graphics device of its own. Two
suites at once oversubscribe the CPU severalfold and everything on the machine slows down
together, so a suite takes this lock and a second one waits -- the way Bazel waits on its
output base and Cargo on its build directory.

It is an advisory lock on an open file, not a file that exists or a pid written into one:
the kernel drops it when the holder dies, so an agent killed mid-suite leaves nothing
stale behind. The first RECORD bytes hold the holder's description and the byte after them
is what is locked, so writing that description never touches the locked region.

The path is under the home directory rather than the temp directory because run_tests.py
gives every suite process a TMPDIR of its own: a lock keyed on that would be per-process,
which is the opposite of what this is for.
"""

import contextlib
import os
import sys
import time

# What makes a target a test suite -- and so what the lock covers, whether it is launched by
# `just test` or by `just run`.
SUITE_SUFFIX = "_tests"

LOCK_PATH = os.path.join(os.path.expanduser("~"), ".bernini", "suite.lock")

# The holder's description is written into the first RECORD bytes, padded rather than
# truncated so the record is a fixed size and the locked byte never moves.
RECORD = 512

POLL_SECONDS = 0.25
REPORT_SECONDS = 60


def is_suite(target):
    """Whether `target` names a test suite, and so runs under the lock."""
    return target.endswith(SUITE_SUFFIX)


if sys.platform == "win32":
    import msvcrt

    def _try_lock(fd):
        os.lseek(fd, RECORD, os.SEEK_SET)
        try:
            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False

    def _unlock(fd):
        os.lseek(fd, RECORD, os.SEEK_SET)
        msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)

else:
    import fcntl

    def _try_lock(fd):
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False

    def _unlock(fd):
        fcntl.flock(fd, fcntl.LOCK_UN)


def describe():
    """This process as a waiter should see it: the checkout it runs in, and its pid."""
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return f"{os.path.basename(root)} (pid {os.getpid()})"


def elapsed(seconds):
    """`seconds` as a waiting message spells a duration: 45s, 2m14s."""
    seconds = int(seconds)
    return f"{seconds}s" if seconds < 60 else f"{seconds // 60}m{seconds % 60:02d}s"


def _write_holder(fd, label):
    os.lseek(fd, 0, os.SEEK_SET)
    os.write(fd, f"{time.time():.0f}\t{label}".encode()[:RECORD].ljust(RECORD))


def read_holder(path=LOCK_PATH):
    """Who holds the lock and for how long, as (label, seconds), or None if unreadable.

    Best effort by design: the record is written just after the lock is taken, so a waiter
    that reads in between sees a stale or empty one and must say nothing rather than lie.
    """
    # Unbuffered: a buffered read asks for more than RECORD bytes in one call, which reaches
    # the locked byte past the record, and a Windows lock is mandatory rather than advisory.
    try:
        with open(path, "rb", buffering=0) as handle:
            record = handle.read(RECORD)
    except OSError:
        return None

    started, tab, label = record.decode("utf-8", "replace").strip().partition("\t")
    if not tab:
        return None
    try:
        return label, max(0.0, time.time() - float(started))
    except ValueError:
        return None


def _report(path, out):
    held = read_holder(path)
    if held:
        label, age = held
        print(f"waiting for the test suite lock: {label} has held it for {elapsed(age)}.",
              file=out, flush=True)
    else:
        print("waiting for the test suite lock, held by another checkout.", file=out, flush=True)


def _acquire(fd, path, out):
    """Take the lock, waiting out the current holder. Returns the seconds spent waiting."""
    if _try_lock(fd):
        return 0.0

    started = time.monotonic()
    next_report = 0.0
    while not _try_lock(fd):
        waiting = time.monotonic() - started
        if waiting >= next_report:
            _report(path, out)
            next_report = waiting + REPORT_SECONDS
        time.sleep(POLL_SECONDS)

    waited = time.monotonic() - started
    print(f"(waited {elapsed(waited)} for the test suite lock)", file=out, flush=True)
    return waited


@contextlib.contextmanager
def suite_lock(enabled=True, path=LOCK_PATH, out=None):
    """Hold the machine-wide suite lock for the block, waiting for whoever has it.

    Names the holder as soon as it has to wait and again every REPORT_SECONDS, so a run that
    goes quiet says why. With `enabled` false it acquires nothing and yields silently, which
    is what --no-lock passes.
    """
    if not enabled:
        yield
        return

    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd = os.open(path, os.O_RDWR | os.O_CREAT | getattr(os, "O_BINARY", 0), 0o644)
    try:
        _acquire(fd, path, out or sys.stdout)
        _write_holder(fd, describe())
        try:
            yield
        finally:
            _unlock(fd)
    finally:
        os.close(fd)
