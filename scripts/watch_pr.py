#!/usr/bin/env python3
"""Block until a pull request sees new submitted review activity, or merges.

The review hand-off loop (`bcp-feature` opens a slice PR, a human reviews and
merges it) needs a deterministic way to wait for the human's move instead of an
agent re-deriving "did anything happen yet?" from ad-hoc `gh` calls. This script
is that wait: it snapshots the PR's current activity as a baseline, then polls,
and exits the moment something actionable appears, printing exactly one JSON
object on stdout. Everything else (progress, warnings) goes to stderr, so stdout
stays machine-parseable.

Events, in the order they are checked each poll:

    merged    the PR merged                                  -> exit 0
    closed    the PR closed without merging                  -> exit 0
    review    a review was submitted after the baseline      -> exit 0
    comment   an issue or inline comment arrived after it    -> exit 0
    timeout   --timeout elapsed with no activity             -> exit 3

Only *submitted* reviews count -- a PENDING review is a draft the author has
not sent, so acting on it would read half-written feedback. Inline (thread)
comments are polled separately from issue comments because a bare reply to a
review thread does not reliably surface as a new review.

`--once` skips the waiting entirely and prints a snapshot of the current state
(merge state plus every submitted review and comment), for reconciling a
resumed feature against reality.

Uses the `gh` CLI, found on PATH or in its default Windows install location.
Transient `gh` failures are tolerated up to 5 in a row before erroring (exit 1).

Usage:
    python scripts/watch_pr.py 118                  # wait with the defaults
    python scripts/watch_pr.py 118 --interval 60 --timeout 0   # poll forever
    python scripts/watch_pr.py 118 --once           # snapshot, no waiting
    python scripts/watch_pr.py 118 --repo owner/name
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

MAX_CONSECUTIVE_FAILURES = 5

# Fields per object as `gh pr view --json` spells them.
PR_FIELDS = "state,mergedAt,reviews,comments,url,title"


def find_gh():
    gh = shutil.which("gh")
    if gh:
        return gh
    default = r"C:\Program Files\GitHub CLI\gh.exe"
    if os.name == "nt" and os.path.exists(default):
        return default
    sys.exit("error: gh CLI not found on PATH (install: https://cli.github.com)")


def run_gh(gh, args, repo):
    """Runs gh and returns parsed JSON, or raises RuntimeError with the stderr."""
    cmd = [gh] + args
    if repo:
        cmd += ["--repo", repo]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"gh exited {result.returncode}")
    return json.loads(result.stdout)


def repo_path(gh, repo):
    """The owner/name path for the REST endpoints, resolved once."""
    if repo:
        return repo
    data = run_gh(gh, ["repo", "view", "--json", "nameWithOwner"], None)
    return data["nameWithOwner"]


def fetch(gh, pr, repo, rest_repo):
    """One poll: PR state + submitted reviews + issue comments + inline comments."""
    view = run_gh(gh, ["pr", "view", str(pr), "--json", PR_FIELDS], repo)
    reviews = [r for r in view.get("reviews") or [] if r.get("state") != "PENDING"]
    comments = view.get("comments") or []
    inline = run_gh(gh, ["api", f"repos/{rest_repo}/pulls/{pr}/comments"], None)
    return view, reviews, comments, inline


def key(item):
    """Stable identity for a review or comment, robust to gh versions without ids."""
    ident = item.get("id")
    if ident is not None:
        return str(ident)
    author = (item.get("author") or {}).get("login") or item.get("user", {}).get("login")
    stamp = item.get("submittedAt") or item.get("createdAt") or item.get("created_at")
    return f"{author}@{stamp}"


def summarize_review(r):
    return {
        "author": (r.get("author") or {}).get("login"),
        "state": r.get("state"),
        "submittedAt": r.get("submittedAt"),
        "body": r.get("body", ""),
    }


def summarize_comment(c):
    return {
        "author": (c.get("author") or {}).get("login") or (c.get("user") or {}).get("login"),
        "createdAt": c.get("createdAt") or c.get("created_at"),
        "path": c.get("path"),  # inline comments only; None for issue comments
        "body": c.get("body", ""),
    }


def emit(payload):
    print(json.dumps(payload, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("pr", type=int, help="pull request number")
    parser.add_argument("--repo", help="owner/name (default: the repo of the cwd)")
    parser.add_argument("--interval", type=float, default=30, help="seconds between polls (default 30)")
    parser.add_argument(
        "--timeout", type=float, default=3600,
        help="give up after this many seconds; 0 waits forever (default 3600)")
    parser.add_argument("--once", action="store_true", help="print a snapshot and exit")
    args = parser.parse_args()

    gh = find_gh()
    rest_repo = repo_path(gh, args.repo)

    try:
        view, reviews, comments, inline = fetch(gh, args.pr, args.repo, rest_repo)
    except RuntimeError as e:
        sys.exit(f"error: {e}")

    if args.once:
        emit({
            "event": "snapshot",
            "pr": args.pr,
            "state": view["state"],
            "url": view["url"],
            "reviews": [summarize_review(r) for r in reviews],
            "comments": [summarize_comment(c) for c in comments]
            + [summarize_comment(c) for c in inline],
        })
        return

    if view["state"] != "OPEN":
        emit({"event": view["state"].lower(), "pr": args.pr, "url": view["url"]})
        return

    seen = {key(r) for r in reviews} | {key(c) for c in comments} | {key(c) for c in inline}
    print(
        f"watching PR #{args.pr} ({view['title']}) every {args.interval:g}s; "
        f"baseline: {len(reviews)} reviews, {len(comments) + len(inline)} comments",
        file=sys.stderr, flush=True)

    start = time.monotonic()
    failures = 0
    while True:
        elapsed = time.monotonic() - start
        if args.timeout and elapsed >= args.timeout:
            emit({"event": "timeout", "pr": args.pr, "elapsed": round(elapsed)})
            sys.exit(3)

        time.sleep(args.interval)

        try:
            view, reviews, comments, inline = fetch(gh, args.pr, args.repo, rest_repo)
            failures = 0
        except RuntimeError as e:
            failures += 1
            print(f"warning: poll failed ({failures}/{MAX_CONSECUTIVE_FAILURES}): {e}",
                  file=sys.stderr, flush=True)
            if failures >= MAX_CONSECUTIVE_FAILURES:
                sys.exit(f"error: {MAX_CONSECUTIVE_FAILURES} consecutive poll failures")
            continue

        if view["state"] == "MERGED":
            emit({"event": "merged", "pr": args.pr, "url": view["url"]})
            return
        if view["state"] == "CLOSED":
            emit({"event": "closed", "pr": args.pr, "url": view["url"]})
            return

        new_reviews = [r for r in reviews if key(r) not in seen]
        new_comments = [c for c in comments + inline if key(c) not in seen]
        if new_reviews:
            emit({
                "event": "review",
                "pr": args.pr,
                "url": view["url"],
                "reviews": [summarize_review(r) for r in new_reviews],
                "comments": [summarize_comment(c) for c in new_comments],
            })
            return
        if new_comments:
            emit({
                "event": "comment",
                "pr": args.pr,
                "url": view["url"],
                "comments": [summarize_comment(c) for c in new_comments],
            })
            return


if __name__ == "__main__":
    main()
