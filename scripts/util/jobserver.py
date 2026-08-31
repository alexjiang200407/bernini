#!/usr/bin/env python3
"""One job budget shared by every build running on this machine.

Several checkouts of this repo share one machine (see the workspace's CLAUDE.md), and each build
sizes itself against the whole of it: ninja with no `-j` takes cores+2. Three checkouts building at
once is therefore three times the machine's worth of compilers on one machine's worth of cores, and
every one of them slows down together.

The fix is not a lock. Serialising builds is worse than oversubscribing them -- a checkout building
alone should still get the whole machine. What is wanted is a budget the builds *share*, so one
build takes all of it and three take a third each, decided continuously rather than at launch.

ninja 1.13 is a GNU make jobserver client, so that budget already has a standard: a pool of tokens
held outside any one build, which each build draws from and returns. This module holds the pool.
`MAKEFLAGS` in the returned environment is what points ninja at it.

**Fail-open, always.** A build that cannot reach the pool runs uncapped, which is exactly today's
behaviour. A wrong cap would be a hang, and a hung build is a far worse failure than a loud machine.

Two protocols, because ninja implements two:
  * POSIX -- a fifo holding one byte per token. It only exists while somebody holds it open, so
    every build keeps a read-write handle for its lifetime and the last one out takes the tokens
    with it; the next build finds nobody live and primes a fresh one.
  * Windows -- a named semaphore, whose count the kernel keeps while any handle is open. Same
    lifetime rule, without the priming.
"""

import collections
import contextlib
import errno
import os
import sys

STATE_DIR = os.path.join(os.path.expanduser("~"), ".bernini")

# Where one pool lives. Taken as a parameter rather than read from module state so a test can drive
# a pool of its own: one keyed on the real path would contend with the builds actually running on
# this machine, and would be proving something about a pool it does not own.
Paths = collections.namedtuple("Paths", "fifo setup live semaphore")


def paths_in(state_dir=STATE_DIR, name="jobserver"):
    """The four names one shared pool is made of, under `state_dir`.

    `setup` is held exclusively while the pool is created or torn down, so a build opening the fifo
    cannot race the last holder closing it -- which would leave it reading an empty pool forever.
    `live` is held *shared* for as long as a build is using the pool, so whether it can be taken
    exclusively answers "is anybody else live?", which is what decides between priming and joining.
    """
    return Paths(fifo=os.path.join(state_dir, f"{name}.fifo"),
                 setup=os.path.join(state_dir, f"{name}.setup"),
                 live=os.path.join(state_dir, f"{name}.live"),
                 semaphore=f"bernini_{name}")


def default_tokens():
    """Tokens to prime the pool with: one per core.

    ninja keeps one implicit token per instance on top of what it draws, the way make does, so N
    concurrent builds run up to cores+N compilers rather than cores. That overshoot is bounded by
    the number of builds and is the price of never idling a core.
    """
    return os.cpu_count() or 4


if sys.platform == "win32":
    import ctypes
    from ctypes import wintypes

    def _open_pool(tokens, paths):
        """A named semaphore, created if this is the first build and opened if it is not.

        CreateSemaphoreW on an existing name opens it and leaves its count alone, so a second build
        joins the pool rather than resetting it.
        """
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateSemaphoreW.restype = wintypes.HANDLE
        kernel32.CreateSemaphoreW.argtypes = [ctypes.c_void_p, wintypes.LONG, wintypes.LONG,
                                              wintypes.LPCWSTR]

        handle = kernel32.CreateSemaphoreW(None, tokens, tokens, paths.semaphore)
        if not handle:
            raise OSError(ctypes.get_last_error(), "CreateSemaphoreW failed")

        return handle, f"--jobserver-auth={paths.semaphore}"

    def _close_pool(handle):
        ctypes.WinDLL("kernel32", use_last_error=True).CloseHandle(handle)

else:
    import fcntl

    def _prime(tokens, paths):
        """Replace the fifo with a fresh one holding `tokens` bytes, and return a handle on it.

        Opened O_RDWR rather than O_RDONLY: a read-only open blocks until a writer arrives, and the
        point of holding it is that this process *is* what keeps the pool alive.
        """
        with contextlib.suppress(FileNotFoundError):
            os.unlink(paths.fifo)
        os.mkfifo(paths.fifo, 0o600)

        handle = os.open(paths.fifo, os.O_RDWR | os.O_NONBLOCK)
        os.write(handle, b"+" * tokens)
        return handle

    def _open_pool(tokens, paths):
        live = os.open(paths.live, os.O_RDWR | os.O_CREAT, 0o600)
        try:
            fcntl.flock(live, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as err:
            if err.errno not in (errno.EACCES, errno.EAGAIN):
                raise
            # Somebody is live, so the fifo is already primed -- join it rather than resetting a
            # pool other builds are drawing from.
            handle = os.open(paths.fifo, os.O_RDWR | os.O_NONBLOCK)
        else:
            handle = _prime(tokens, paths)

        # Downgraded, never released: the exclusive attempt above was the question, and the shared
        # hold is the answer other builds will get.
        fcntl.flock(live, fcntl.LOCK_SH)

        return (handle, live), f"--jobserver-auth=fifo:{paths.fifo}"

    def _close_pool(handles):
        handle, live = handles
        os.close(handle)
        os.close(live)


@contextlib.contextmanager
def _setup_lock(paths):
    """Serialises priming against tear-down, so no build opens a pool as its last holder closes."""
    fd = os.open(paths.setup, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        if sys.platform == "win32":
            import msvcrt
            msvcrt.locking(fd, msvcrt.LK_LOCK, 1)
            try:
                yield
            finally:
                os.lseek(fd, 0, os.SEEK_SET)
                msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
        else:
            fcntl.flock(fd, fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(fd, fcntl.LOCK_UN)
    finally:
        os.close(fd)


@contextlib.contextmanager
def shared_budget(env, enabled=True, tokens=None, out=None, state_dir=STATE_DIR):
    """`env` with MAKEFLAGS pointing ninja at this machine's shared token pool.

    Yields the environment to run the build in. On any failure it yields `env` unchanged and says
    so: a build that runs uncapped is what happened before this existed, and a build that hangs
    waiting on a pool it could not join is not.

    @param env Environment to copy and extend.
    @param enabled False to skip the pool entirely and hand back `env`.
    @param tokens Pool size; defaults to one per core.
    @param out Stream for the one line it prints about what it did; defaults to stderr as it is at
                  the time of the call, not as it was at import.
    @param state_dir Directory the pool's files live in; a test passes one of its own.
    """
    out = out if out is not None else sys.stderr

    if not enabled:
        yield env
        return

    # `or` would be wrong here: 0 is falsy, so an explicit --jobs 0 would silently become the
    # per-core default, and a negative one would prime an empty pool (`b"+" * -1` is empty).
    if tokens is None:
        tokens = default_tokens()
    elif tokens < 1:
        print(f"warning: --jobs {tokens} is not a usable budget; building uncapped.", file=out)
        yield env
        return

    try:
        paths = paths_in(state_dir)
        os.makedirs(state_dir, exist_ok=True)
        with _setup_lock(paths):
            pool, auth = _open_pool(tokens, paths)
    except Exception as err:
        print(f"warning: could not join the shared job pool ({err}); building uncapped.", file=out)
        yield env
        return

    try:
        # Appended rather than assigned: MAKEFLAGS may already carry flags, and a build launched
        # from a make that is itself a jobserver must not have its own auth overwritten.
        existing = env.get("MAKEFLAGS", "")
        capped = dict(env)
        capped["MAKEFLAGS"] = f"{existing} {auth}".strip() if existing else auth

        print(f"jobserver: {tokens} tokens shared across this machine's builds", file=out)
        yield capped
    finally:
        with contextlib.suppress(Exception):
            with _setup_lock(paths):
                _close_pool(pool)
