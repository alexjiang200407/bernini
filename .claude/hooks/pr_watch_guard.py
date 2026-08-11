#!/usr/bin/env python3
"""Refuse to end a turn that opened or answered a pull request without watching it.

A Stop hook. `scripts/pr.py` arms an entry per PR it writes to and watch_pr.py
claims the PR with its pid while it runs, so "start the watcher as the last
action of the turn" stops being a rule the agent has to remember. Only entries
armed by this session count -- a later session must not inherit a block for a PR
it never saw -- and only those no live watcher already holds, or answering a
comment mid-watch would demand a second watcher on the same PR.

Exit 2 blocks the stop and sends stderr back to the model. `stop_hook_active`
means the model is already continuing because of this hook, so it is allowed
through the second time rather than being trapped in a loop.
"""

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))

from util import watchlist  # noqa: E402


def main():
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        payload = {}
    if payload.get("stop_hook_active"):
        return 0

    entries = watchlist.pending()
    if not entries:
        return 0

    lines = "\n".join(f"    just watch-pr {e['pr']}    # {e.get('url', '')}" for e in entries)
    print("Blocked by .claude/hooks/pr_watch_guard.py.\n\n"
          "A pull request you wrote to this turn has nothing waiting on it, so a review "
          "would land in silence.\nStart the watcher -- in the BACKGROUND, so this turn ends "
          "and the user gets their session back:\n\n"
          f"{lines}\n\n"
          "It claims the PR as it starts, so this hook is satisfied the moment it is running, and "
          "it wakes you\nwhen a review, a comment or the merge arrives. One watcher per PR: if one "
          "is already running it\nnames the pid and exits, which is a satisfied hook, not a failure. "
          "If the user has to decide\nsomething first, release it with `just pr unwatch <n>` and say "
          "why.",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
