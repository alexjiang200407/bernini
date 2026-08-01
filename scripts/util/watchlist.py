"""PRs this session opened that nothing is watching yet.

`bcp-feature` ends a turn by blocking on watch_pr.py, because a PR nobody is
waiting on is a PR whose review lands into silence. That is a rule an agent can
forget, so it is recorded instead: `pr.py create` arms an entry here, watch_pr.py
disarms it when it starts watching, and the Stop hook refuses to end a turn while
anything is still armed.

The state lives in .git/, which is per-clone and never committed, and every entry
carries the session that armed it -- a later session must not inherit a block for
a PR it knows nothing about.
"""

import json
import os

from . import cmake_tools as ct

PATH = os.path.join(ct.REPO_ROOT, ".git", "bernini-pr-watch.json")


def _session():
    return os.environ.get("CLAUDE_CODE_SESSION_ID", "")


def _load():
    try:
        with open(PATH, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return []
    return data.get("pending", []) if isinstance(data, dict) else []


def _save(pending):
    try:
        with open(PATH, "w", encoding="utf-8") as fh:
            json.dump({"pending": pending}, fh, indent=2)
    except OSError:
        pass


def arm(pr, url=""):
    """Record that PR `pr` needs a watcher before this turn may end."""
    pending = [e for e in _load() if e.get("pr") != pr or e.get("session") != _session()]
    pending.append({"pr": pr, "url": url, "session": _session()})
    _save(pending)


def disarm(pr):
    """Drop `pr` from every session's list; it is being watched, or is done."""
    _save([e for e in _load() if e.get("pr") != pr])


def pending(session=None):
    """Armed entries for `session` (the current one by default)."""
    want = _session() if session is None else session
    return [e for e in _load() if e.get("session") == want]
