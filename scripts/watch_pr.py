#!/usr/bin/env python3
"""Block until a pull request sees new review activity, fails CI, or merges.

The review hand-off loop (`bcp-feature` opens a slice PR, a human reviews and
merges it) needs a deterministic way to wait for the human's move instead of an
agent re-deriving "did anything happen yet?" from ad-hoc `gh` calls. This script
is that wait: it snapshots the PR's current activity as a baseline, then polls,
and exits the moment something actionable appears, printing exactly one JSON
object on stdout. Everything else (progress, warnings) goes to stderr, so stdout
stays machine-parseable.

Events, in the order they are checked each poll:

    merged      the PR merged                                  -> exit 0
    closed      the PR closed without merging                  -> exit 0
    ci_failure  a check on the head commit concluded red       -> exit 0
    review      a review was submitted after the baseline      -> exit 0
    comment     an issue or inline comment arrived after it    -> exit 0
    timeout     --timeout elapsed with no activity             -> exit 3

`ci_failure` is checked ahead of the human events because a review of code that
does not compile is answered by fixing the build first. Nothing is lost by that
order: the event carries whatever review or comment was also waiting, and each of
its `checks` carries the failed job's diagnostics lifted out of the log, so the
caller has the compiler errors in hand rather than a URL to go read.

A check is identified by commit, name *and* conclusion time, so a job that fails
again after a push -- or is re-run and fails again on the same commit -- is a new
event rather than one already behind the baseline. A cancelled check is never
reported: `ci.yml` sets `cancel-in-progress`, so every push past the first cancels
the run it superseded. A build already red when the watch starts is baselined away
like any other prior activity; `--once` is what reports the current state.

Only *submitted* reviews count -- a PENDING review is a draft the author has
not sent, so acting on it would read half-written feedback. Inline (thread)
comments are polled separately from issue comments because a bare reply to a
review thread does not reliably surface as a new review.

`--once` skips the waiting entirely and prints a snapshot of the current state
(merge state, every failing check, and every submitted review and comment), for
reconciling a resumed feature against reality.

`--since <ISO-8601>` closes the hand-off race: activity is normally baselined
at startup, so a comment that arrives after the caller gathered the feedback it
is responding to but before the next watch starts would vanish into the new
baseline. Passing the timestamp of the newest item already handled keeps
everything after it out of the baseline, and the first check fires immediately
if anything is waiting.

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
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from util import watchlist  # noqa: E402
from util.gh import BOT_LOGIN  # noqa: E402

MAX_CONSECUTIVE_FAILURES = 5

# Events after which the PR is again waiting on nobody, so the Stop hook must ask
# for another watch. `merged` and `closed` are the two that end the loop for good.
REARMING = ("ci_failure", "review", "comment", "timeout")

# Fields per object as `gh pr view --json` spells them.
PR_FIELDS = "state,mergedAt,reviews,comments,url,title,headRefOid,statusCheckRollup"

# CANCELLED is deliberately absent -- see the module docstring. SKIPPED and NEUTRAL
# are not failures either.
FAILED_CONCLUSIONS = {"FAILURE", "TIMED_OUT", "ACTION_REQUIRED", "STARTUP_FAILURE", "ERROR"}

# What a compiler, ninja, CMake or the runner writes when something breaks. The rest
# of a build log is thousands of lines of progress and echoed command lines.
#
# Warnings are kept because the strict presets compile with /WX: MSVC then reports
# `error C2220: the following warning is treated as an error` and leaves the warning
# that caused it on the next line. Notes are kept for the same reason -- C4458's note
# is the only line naming what the declaration collides with.
DIAGNOSTIC = re.compile(
    r"""(?: \b(?:fatal\s+)?(?:error|warning|note)\b\s*[A-Z]?\d*\s*:  # cl / clang / gcc
        | ^FAILED:                                                   # ninja
        | \#\#\[error\]                                              # the runner's own
        | \bCMake\s+Error\b
        )""",
    re.IGNORECASE | re.VERBOSE)

# CMake's own status and annotation lines, which carry "Note:" and so would otherwise
# read as diagnostics for every port vcpkg configures.
NOISE = re.compile(r"^\s*(?:--|#\s)")

# `<job>\t<step>\t2026-08-03T10:59:39.7577226Z ` heads every line the API returns.
LOG_PREFIX = re.compile(r"^(?:[^\t]*\t)*\d{4}-\d\d-\d\dT[\d:.]+Z\s?")

MAX_DIAGNOSTICS = 60


def find_gh():
    gh = shutil.which("gh")
    if gh:
        return gh
    default = r"C:\Program Files\GitHub CLI\gh.exe"
    if os.name == "nt" and os.path.exists(default):
        return default
    sys.exit("error: gh CLI not found on PATH (install: https://cli.github.com)")


def run_gh_text(gh, args, repo):
    """Runs gh and returns stdout, or raises RuntimeError with the stderr."""
    cmd = [gh] + args
    if repo:
        cmd += ["--repo", repo]
    # gh emits UTF-8; text=True alone would decode it with the ANSI codepage.
    result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"gh exited {result.returncode}")
    return result.stdout


def run_gh(gh, args, repo):
    """Runs gh and returns parsed JSON, or raises RuntimeError with the stderr."""
    return json.loads(run_gh_text(gh, args, repo))


def repo_path(gh, repo):
    """The owner/name path for the REST endpoints, resolved once."""
    if repo:
        return repo
    data = run_gh(gh, ["repo", "view", "--json", "nameWithOwner"], None)
    return data["nameWithOwner"]


def failed_checks(view):
    """The head commit's concluded-red checks, normalized to the comment shape.

    Keyed by commit and conclusion time as well as name, so a job that fails again
    after a push -- or is re-run and fails again on the same commit -- is a new event
    rather than one already behind the baseline.
    """
    sha = view.get("headRefOid") or ""
    out = []
    for check in view.get("statusCheckRollup") or []:
        # A CheckRun reports `conclusion`; a legacy commit status reports `state`.
        verdict = (check.get("conclusion") or check.get("state") or "").upper()
        if verdict not in FAILED_CONCLUSIONS:
            continue
        completed = check.get("completedAt") or ""
        name = check.get("name") or check.get("context") or "check"
        out.append({
            "id": f"{sha[:7]}:{name}@{completed}",
            "name": name,
            "workflow": check.get("workflowName"),
            "conclusion": verdict,
            "completedAt": completed,
            "sha": sha,
            "url": check.get("detailsUrl") or check.get("targetUrl"),
        })
    return out


def fetch(gh, pr, repo, rest_repo):
    """One poll: PR state + failed checks + submitted reviews + issue and inline comments."""
    view = run_gh(gh, ["pr", "view", str(pr), "--json", PR_FIELDS], repo)
    reviews = [r for r in view.get("reviews") or [] if r.get("state") != "PENDING"]
    comments = view.get("comments") or []
    inline = run_gh(gh, ["api", f"repos/{rest_repo}/pulls/{pr}/comments"], None)
    return view, reviews, comments, inline, failed_checks(view)


def key(item):
    """Stable identity for a review, comment or check, robust to gh versions without ids."""
    ident = item.get("id")
    if ident is not None:
        return str(ident)
    author = (item.get("author") or {}).get("login") or item.get("user", {}).get("login")
    return f"{author}@{stamp(item)}"


def stamp(item):
    return (item.get("submittedAt") or item.get("createdAt")
            or item.get("created_at") or item.get("completedAt") or "")


def by_the_agent(item):
    """Did the bot write this? Waking for it would be reading yourself.

    A baseline time cannot decide this: a watch running in the background is
    still running when the agent posts its replies, so its own words arrive
    after any startup baseline no matter how the baseline was chosen. The author
    is the durable answer. GraphQL spells the login `morgana-coding-agent` and
    REST spells it `morgana-coding-agent[bot]`, hence the suffix strip.
    """
    login = ((item.get("author") or {}).get("login")
             or (item.get("user") or {}).get("login") or "")
    return login.lower().removesuffix("[bot]") == BOT_LOGIN.lower().removesuffix("[bot]")


def summarize_review(r):
    return {
        "author": (r.get("author") or {}).get("login"),
        "state": r.get("state"),
        "submittedAt": r.get("submittedAt"),
        "body": r.get("body", ""),
    }


def rest_id(c):
    """The numeric comment id, which is the only form `pr.py reply` can use.

    Inline comments arrive from the REST endpoint already numeric. Issue comments come from
    `gh pr view --json comments`, which is GraphQL and returns a node id (`IC_kwDO...`) that no
    REST route accepts, so the number is taken off the permalink instead.
    """
    ident = c.get("id")
    if isinstance(ident, int) or (isinstance(ident, str) and ident.isdigit()):
        return int(ident)
    found = re.search(r"#issuecomment-(\d+)", c.get("url") or c.get("html_url") or "")
    return int(found.group(1)) if found else ident


def summarize_comment(c):
    """One comment, flattened.

    `path` is set only on an inline comment, and is what tells them apart: an inline one belongs to
    a review thread and must be answered in it. Either way the answer goes through

        just pr reply {replyTo} --body-file <file>

    which routes by looking the id up, so the id is carried here rather than left to a second
    lookup.
    """
    return {
        "author": (c.get("author") or {}).get("login") or (c.get("user") or {}).get("login"),
        "createdAt": c.get("createdAt") or c.get("created_at"),
        "path": c.get("path"),  # inline comments only; None for issue comments
        "line": c.get("line") or c.get("original_line"),
        "replyTo": rest_id(c),
        "url": c.get("html_url") or c.get("url"),
        "body": c.get("body", ""),
    }


def diagnostics(log):
    """The lines of a job log worth reading, in the order the job wrote them.

    A failed build log runs to thousands of lines of progress and echoed command
    lines; the diagnostics are a few dozen of them. Each line's job/step/timestamp
    prefix is stripped so the text reads as the tool wrote it.

    Truncation keeps the *first* diagnostics, since the errors after the first are
    usually its cascade -- but the *last* lines when nothing matched at all, because
    a job that died some other way (a missing tool, a killed runner) says so at the
    point it stopped.
    """
    lines = [LOG_PREFIX.sub("", line).rstrip() for line in log.splitlines() if line.strip()]
    hits = [line for line in lines if DIAGNOSTIC.search(line) and not NOISE.match(line)]
    return hits[:MAX_DIAGNOSTICS] if hits else lines[-MAX_DIAGNOSTICS:]


def job_log(gh, check, repo):
    """The failed steps of the check's job, reduced to diagnostics; [] if unavailable.

    Best-effort by design: the log is an extra round trip to a second endpoint, and a
    check whose log has expired, is still uploading, or belongs to a non-Actions
    provider must not cost the caller the event itself.
    """
    found = re.search(r"/actions/runs/(\d+)(?:/job/(\d+))?", check.get("url") or "")
    if not found:
        return []
    run, job = found.group(1), found.group(2)
    args = ["run", "view", run, "--log-failed"] + (["--job", job] if job else [])
    try:
        return diagnostics(run_gh_text(gh, args, repo))
    except (RuntimeError, OSError) as e:
        print(f"warning: no log for {check['name']}: {e}", file=sys.stderr, flush=True)
        return []


def summarize_check(gh, check, repo):
    return dict(check, log=job_log(gh, check, repo))


def emit(payload):
    """Prints the one event, and re-arms the watch if the PR still needs answering.

    The baseline recorded is the newest item just reported, so a watch restarted
    without an answer in between does not fire on the same feedback twice.
    """
    event = payload.get("event")
    if event in REARMING:
        reported = ((payload.get("reviews") or []) + (payload.get("comments") or [])
                    + (payload.get("checks") or []))
        newest = max((stamp(i) for i in reported), default="")
        watchlist.arm(payload["pr"], payload.get("url", ""), newest or None)
    elif event in ("merged", "closed"):
        watchlist.disarm(payload["pr"])
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
    parser.add_argument(
        "--since",
        help="ISO-8601 UTC time (2026-07-29T13:13:22Z); only activity at or before it joins the "
        "baseline, and anything already waiting fires immediately. Defaults to the time pr.py last "
        "posted to this PR, which is what keeps the watch from firing on its own reply")
    args = parser.parse_args()

    since = args.since or watchlist.since_for(args.pr)

    # Claim the PR before the first poll, not after the last one: this process *is*
    # the wait, so the Stop hook must see it as satisfied while it runs in the
    # background. emit() re-arms when it exits with the PR still needing an answer.
    if not args.once:
        running = watchlist.watcher(args.pr)
        if running:
            print(f"PR #{args.pr} is already watched by pid {running}; not starting a second watcher.",
                  file=sys.stderr)
            return
        watchlist.claim(args.pr)

    gh = find_gh()
    rest_repo = repo_path(gh, args.repo)

    try:
        view, reviews, comments, inline, checks = fetch(gh, args.pr, args.repo, rest_repo)
    except RuntimeError as e:
        sys.exit(f"error: {e}")

    if args.once:
        emit({
            "event": "snapshot",
            "pr": args.pr,
            "state": view["state"],
            "url": view["url"],
            "checks": [summarize_check(gh, c, args.repo) for c in checks],
            "reviews": [summarize_review(r) for r in reviews],
            "comments": [summarize_comment(c) for c in comments]
            + [summarize_comment(c) for c in inline],
        })
        return

    if view["state"] != "OPEN":
        emit({"event": view["state"].lower(), "pr": args.pr, "url": view["url"]})
        return

    # GitHub stamps are Z-suffixed UTC, so string comparison is chronological.
    def baselined(item):
        return since is None or stamp(item) <= since

    items = reviews + comments + inline + checks
    seen = {key(x) for x in items if baselined(x)}
    print(
        f"watching PR #{args.pr} ({view['title']}) every {args.interval:g}s; "
        f"baseline: {len(seen)} of {len(items)} items"
        + (f", since {since}" if since else ""),
        file=sys.stderr, flush=True)

    def actionable(view, reviews, comments, inline, checks):
        """Emits and reports True when the poll holds an event; the caller then exits."""
        if view["state"] == "MERGED":
            emit({"event": "merged", "pr": args.pr, "url": view["url"]})
            return True
        if view["state"] == "CLOSED":
            emit({"event": "closed", "pr": args.pr, "url": view["url"]})
            return True

        new_reviews = [r for r in reviews if key(r) not in seen and not by_the_agent(r)]
        new_comments = [c for c in comments + inline
                        if key(c) not in seen and not by_the_agent(c)]
        new_checks = [c for c in checks if key(c) not in seen]
        if new_checks:
            emit({
                "event": "ci_failure",
                "pr": args.pr,
                "url": view["url"],
                "sha": view.get("headRefOid"),
                "checks": [summarize_check(gh, c, args.repo) for c in new_checks],
                "reviews": [summarize_review(r) for r in new_reviews],
                "comments": [summarize_comment(c) for c in new_comments],
            })
            return True
        if new_reviews:
            emit({
                "event": "review",
                "pr": args.pr,
                "url": view["url"],
                "reviews": [summarize_review(r) for r in new_reviews],
                "comments": [summarize_comment(c) for c in new_comments],
            })
            return True
        if new_comments:
            emit({
                "event": "comment",
                "pr": args.pr,
                "url": view["url"],
                "comments": [summarize_comment(c) for c in new_comments],
            })
            return True
        return False

    # With a baseline time, the race-window items are already in hand -- fire before sleeping.
    if actionable(view, reviews, comments, inline, checks):
        return

    start = time.monotonic()
    failures = 0
    while True:
        elapsed = time.monotonic() - start
        if args.timeout and elapsed >= args.timeout:
            emit({"event": "timeout", "pr": args.pr, "elapsed": round(elapsed)})
            sys.exit(3)

        time.sleep(args.interval)

        try:
            view, reviews, comments, inline, checks = fetch(gh, args.pr, args.repo, rest_repo)
            failures = 0
        except RuntimeError as e:
            failures += 1
            print(f"warning: poll failed ({failures}/{MAX_CONSECUTIVE_FAILURES}): {e}",
                  file=sys.stderr, flush=True)
            if failures >= MAX_CONSECUTIVE_FAILURES:
                sys.exit(f"error: {MAX_CONSECUTIVE_FAILURES} consecutive poll failures")
            continue

        if actionable(view, reviews, comments, inline, checks):
            return


if __name__ == "__main__":
    main()
