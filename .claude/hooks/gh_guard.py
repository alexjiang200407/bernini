#!/usr/bin/env python3
"""Reject the pull-request commands that bypass the project's PR rules.

A PreToolUse hook on Bash/PowerShell. The rules it enforces are the ones an
agent reliably knows and intermittently forgets: a PR body is written by the
bot, an inline review comment is answered inside its thread, a commit keeps its
attribution, and nobody merges but the user. Each rejection names the command to
use instead, so being blocked is a redirection rather than a dead end.

Exit 2 blocks the call and sends stderr back to the model; exit 0 allows it.
Anything unrecognised is allowed -- this is a guard rail on a known set of
mistakes, not an allowlist. See docs/ai-coding.md.
"""

import json
import os
import re
import shlex
import sys

SEPARATORS = re.compile(r"&&|\|\||[;|\n]")

USE_CREATE = ("Open the pull request through the script, so the body's title is lifted from the\n"
              "file and the watch is armed. Write the body to a file, headed by '# the title':\n"
              "    just pr create --base <branch> --body-file <file>\n"
              "It opens the PR as you, not as the bot -- a squash merge takes the commit's author\n"
              "from the PR's author, so a bot-authored PR signs every line of master as the bot's.")

USE_REPLY = ("Review feedback is answered where it was left. Get the ids with\n"
             "    just pr comments <n>\n"
             "then answer each one with\n"
             "    just pr reply <comment-id> --body-file <file>\n"
             "which posts in the thread when the id is an inline comment. A top-level summary,\n"
             "once every thread has a reply, is `just pr comment <n> --body-file <file>`.")

USE_EDIT = ("Edit a pull request through the bot so the body stays the bot's:\n"
            "    just pr edit <n> --title \"...\" --body-file <file>")

NEVER_MERGE = ("Never merge or close a pull request. Continuous review only works if a human\n"
               "approves; the user merges. Report that it is ready and stop.")

KEEP_HOOK = ("--no-verify skips .githooks/prepare-commit-msg, which is what co-authors the\n"
             "commit to the bot. Commit without it. If a hook is failing, fix the cause.")

# (tool, matcher over the argument tokens, message)
GH_RULES = [
    (("pr", "create"), USE_CREATE),
    (("pr", "comment"), USE_REPLY),
    (("pr", "review"), USE_REPLY),
    (("pr", "edit"), USE_EDIT),
    (("pr", "merge"), NEVER_MERGE),
    (("pr", "close"), NEVER_MERGE),
    (("pr", "ready"), NEVER_MERGE),
]

WRITE_FLAGS = {"-X", "--method", "-f", "--field", "-F", "--raw-field", "--input"}
WRITE_METHODS = {"POST", "PATCH", "PUT", "DELETE"}
COMMENT_ENDPOINT = re.compile(r"(pulls|issues)/\d+/(comments|reviews)|comments/\d+/replies")


def segments(command):
    """The command split into separately-executed pieces, each as tokens."""
    for raw in SEPARATORS.split(command):
        raw = raw.strip()
        if not raw:
            continue
        try:
            tokens = shlex.split(raw, posix=False)
        except ValueError:
            tokens = raw.split()
        yield [t.strip("\"'") for t in tokens]


def tool_args(tokens, name):
    """The arguments following the first invocation of `name` in `tokens`, or None."""
    for i, token in enumerate(tokens):
        base = os.path.basename(token.replace("\\", "/")).lower()
        if base in (name, f"{name}.exe"):
            return tokens[i + 1:]
    return None


def gh_verdict(args):
    positional = [a for a in args if not a.startswith("-")]
    for prefix, message in GH_RULES:
        if tuple(positional[:len(prefix)]) == prefix:
            return message

    if positional[:1] == ["api"]:
        writes = any(a in WRITE_FLAGS for a in args) or any(a in WRITE_METHODS for a in args)
        endpoint = positional[1] if len(positional) > 1 else ""
        if writes and COMMENT_ENDPOINT.search(endpoint):
            return USE_REPLY
    return None


def git_verdict(args):
    if args[:1] == ["commit"] and ({"--no-verify", "-n"} & set(args)):
        return KEEP_HOOK
    return None


def main():
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return 0
    command = (payload.get("tool_input") or {}).get("command") or ""

    for tokens in segments(command):
        for name, verdict in (("gh", gh_verdict), ("git", git_verdict)):
            args = tool_args(tokens, name)
            if args is None:
                continue
            message = verdict(args)
            if message:
                print(f"Blocked by .claude/hooks/gh_guard.py.\n\n{message}", file=sys.stderr)
                return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
