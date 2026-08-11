"""PRs this session opened that nothing is watching yet.

`bcp-feature` ends a turn by blocking on watch_pr.py, because a PR nobody is
waiting on is a PR whose review lands into silence. That is a rule an agent can
forget, so it is recorded instead: `pr.py create` arms an entry here, watch_pr.py
records its pid against the PR when it starts watching, and the Stop hook refuses
to end a turn while anything is armed that nothing is watching.

Liveness, not history, is what the watching half records -- a PR is watched only
while the process claiming it is alive, so the hook blocks again the moment a
watcher dies, and a second watch on a PR someone already holds is refused. A
watcher therefore gives up its claim by exiting rather than by writing: an
explicit release would have one process rewriting `watching` at the moment
another claims it -- one watch ending as the next begins is the normal handoff --
and the loser of that read-modify-write drops the winner's claim on the floor.

The state lives in the git dir, which is per-worktree and never committed. Armed
entries carry the session that armed them -- a later session must not inherit a
block for a PR it knows nothing about -- while claims do not: a watcher started
by another session is still a watcher.
"""

import json
import os
import subprocess
import sys

from . import cmake_tools as ct


def _git_dir():
    path = os.path.join(ct.REPO_ROOT, ".git")
    if os.path.isfile(path):  # linked worktree: .git is a pointer file
        try:
            with open(path, encoding="utf-8") as fh:
                head = fh.read().strip()
        except OSError:
            return path
        if head.startswith("gitdir:"):
            gitdir = head[len("gitdir:") :].strip()
            if not os.path.isabs(gitdir):
                gitdir = os.path.normpath(os.path.join(ct.REPO_ROOT, gitdir))
            return gitdir
    return path


PATH = os.path.join(_git_dir(), "bernini-pr-watch.json")


def _session():
    return os.environ.get("CLAUDE_CODE_SESSION_ID", "")


def _doc():
    try:
        with open(PATH, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def _load():
    return _doc().get("pending", [])


def _save(pending=None, watching=None):
    data = _doc()
    if pending is not None:
        data["pending"] = pending
    if watching is not None:
        data["watching"] = watching
    tmp = f"{PATH}.{os.getpid()}"
    try:
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=2)
        os.replace(tmp, PATH)
    except OSError:
        try:
            os.unlink(tmp)
        except OSError:
            pass


def _alive(pid):
    """Whether `pid` is a running watcher. An uncertain answer is False.

    Asks for the command line rather than whether the pid exists: a recycled pid
    would otherwise suppress the Stop hook for good, while a watcher missed here
    costs only the duplicate its caller refuses. `os.kill(pid, 0)` is not the
    probe it looks like -- on Windows it terminates the process rather than
    testing it.
    """
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if sys.platform == "win32":
        cmd = ["powershell", "-NoProfile", "-Command",
               f"(Get-CimInstance Win32_Process -Filter 'ProcessId={pid}').CommandLine"]
    else:
        cmd = ["ps", "-p", str(pid), "-o", "command="]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return False
    return "watch_pr" in out.stdout


def _live_claims(persist=False):
    """Claims whose watcher still exists.

    The pruning is written back only when asked. `pending()` runs on every Stop,
    and a read that writes races the watcher it is reading about -- a stale claim
    left in the file costs nothing, because liveness is decided on every read.
    """
    claims = _doc().get("watching", [])
    live = [e for e in claims if _alive(e.get("pid"))]
    if persist and len(live) != len(claims):
        _save(watching=live)
    return live


def arm(pr, url="", since=None):
    """Record that PR `pr` needs a watcher before this turn may end.

    `since` is the server timestamp of whatever was just posted. Carrying it here
    is what stops the next watch from firing on the agent's own reply: the caller
    would otherwise have to hand the watcher a timestamp it guessed.
    """
    kept, previous = [], None
    for entry in _load():
        if entry.get("pr") == pr:
            previous = previous or entry.get("since")
            if entry.get("session") == _session():
                continue
        kept.append(entry)
    kept.append({"pr": pr, "url": url, "session": _session(), "since": since or previous})
    _save(kept)


def disarm(pr):
    """Drop `pr` from every session's list; it is being watched, or is done."""
    _save([e for e in _load() if e.get("pr") != pr])


def claim(pr):
    """Record this process as the watcher of `pr`.

    The pending entry is deliberately left armed. `pending()` filters a PR that
    has a live watcher, so the entry costs nothing while the watch runs and is
    what re-blocks the Stop hook if the watcher dies before reporting anything --
    the alternative, disarming here, loses the PR entirely when the first poll
    fails.
    """
    watching = [e for e in _live_claims() if e.get("pr") != pr]
    watching.append({"pr": pr, "pid": os.getpid()})
    _save(watching=watching)


def watcher(pr):
    """The pid already watching `pr`, or None.

    Guards a watch started after another, which is how duplicates actually arise;
    two launched at the same instant can still both find the list empty.
    """
    return next((e["pid"] for e in _live_claims(persist=True) if e.get("pr") == pr), None)


def pending(session=None):
    """Armed entries for `session` (the current one by default) that nothing is watching.

    A PR with a live watcher is not pending however many times it has been armed
    since: the watcher polls the PR itself, so the later comment it was armed for
    is one it will report. Filtering here rather than at `arm` is what keeps a
    reply posted mid-watch from demanding a second watcher.
    """
    want = _session() if session is None else session
    entries = [e for e in _load() if e.get("session") == want]
    if not entries:
        return []
    watched = {e.get("pr") for e in _live_claims()}
    return [e for e in entries if e.get("pr") not in watched]


def since_for(pr):
    """The newest recorded post time for `pr`, whichever session posted it."""
    stamps = [e.get("since") for e in _load() if e.get("pr") == pr and e.get("since")]
    return max(stamps) if stamps else None
