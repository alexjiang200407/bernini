#!/usr/bin/env python3
"""Delete the local branches whose pull request merged, and nothing else.

Usage:
    python scripts/cleanup_branches.py             # list what would go, delete nothing
    python scripts/cleanup_branches.py --delete    # delete them
    python scripts/cleanup_branches.py --json      # the classification, for an agent
    python scripts/cleanup_branches.py --delete --remote   # also push the deletes origin missed

Merged is decided by GitHub, not by git. This repository squash-merges, so a merged
branch's commits are not ancestors of master and `git branch --merged` reports every one
of them as unmerged -- which is why nobody ever cut any. `gh pr list` is the only source
that knows.

Everything else is kept, and the kept ones are printed with the reason. A closed pull
request is the sharp case: its work is by definition not on master, so a closed branch is
the one thing here that would actually lose something.
"""

import argparse
import json
import subprocess
import sys

import util.cmake_tools as ct
import util.gh as gh

# Never deleted whatever GitHub says: the branch this ran from, and the trunk.
PROTECTED = {"master", "main"}


def git(*args):
    # At the repo root, not the ambient cwd, so this runs from anywhere -- as util.gh
    # already does for every `gh` call.
    out = subprocess.run(["git", *args], capture_output=True, text=True, cwd=ct.REPO_ROOT)
    if out.returncode != 0:
        sys.exit(f"git {' '.join(args)} failed:\n{out.stderr.strip()}")
    return out.stdout


def pull_requests():
    """Every pull request's head branch and state, keyed by head branch.

    Read as the logged-in developer, not as the bot: util.gh's token is for *writing*
    to a pull request, and this only ever reads.
    """
    try:
        prs = gh.gh_json(["pr", "list", "--state", "all", "--limit", "1000",
                          "--json", "number,state,headRefName"])
    except gh.GhError as err:
        sys.exit(f"gh pr list failed -- is the GitHub CLI authenticated?\n{err}")

    by_head = {}
    for pr in prs or []:
        by_head.setdefault(pr["headRefName"], []).append(pr)
    return by_head


def classify(by_head):
    """Every local branch, as (name, verdict, reason, prs).

    A branch with several pull requests takes the strongest: merged beats open beats
    closed, so a branch reopened after its merge is still merged.
    """
    fields = "%(refname:short)\t%(worktreepath)\t%(HEAD)"
    rows = []
    for line in git("for-each-ref", f"--format={fields}", "refs/heads").splitlines():
        name, worktree, head = (line.split("\t") + ["", "", ""])[:3]
        prs = by_head.get(name, [])
        states = {pr["state"] for pr in prs}
        numbers = ", ".join(f"#{pr['number']}" for pr in prs)

        if head == "*":
            rows.append((name, "keep", "the current branch", numbers))
        elif name in PROTECTED:
            rows.append((name, "keep", "protected", numbers))
        elif worktree:
            rows.append((name, "keep", f"checked out at {worktree}", numbers))
        elif "MERGED" in states:
            rows.append((name, "delete", f"{numbers} merged", numbers))
        elif "OPEN" in states:
            rows.append((name, "keep", f"{numbers} still open", numbers))
        elif "CLOSED" in states:
            rows.append((name, "keep", f"{numbers} closed unmerged -- its work is not on master", numbers))
        else:
            rows.append((name, "keep", "no pull request", numbers))
    return rows


def report(rows):
    doomed = [r for r in rows if r[1] == "delete"]
    kept = [r for r in rows if r[1] == "keep"]
    width = max((len(r[0]) for r in rows), default=0)

    print(f"delete ({len(doomed)}) -- pull request merged")
    for name, _, reason, _ in sorted(doomed):
        print(f"  {name:<{width}}  {reason}")

    print(f"\nkeep ({len(kept)})")
    for name, _, reason, _ in sorted(kept):
        print(f"  {name:<{width}}  {reason}")
    return doomed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--delete", action="store_true", help="Actually delete; without it nothing is written.")
    parser.add_argument("--remote", action="store_true", help="Also delete the merged branches origin still carries.")
    parser.add_argument("--json", action="store_true", help="Emit the classification instead of the listing.")
    parser.add_argument("--no-fetch", action="store_true", help="Skip the fetch --prune this normally starts with.")
    args = parser.parse_args()

    if not args.no_fetch:
        git("fetch", "--prune", "origin")

    rows = classify(pull_requests())

    if args.json:
        print(json.dumps([{"branch": n, "verdict": v, "reason": r, "prs": p} for n, v, r, p in rows], indent=2))
        return

    doomed = report(rows)
    if not doomed:
        print("\nnothing to delete.")
        return

    if not args.delete:
        print(f"\n{len(doomed)} branches would be deleted. Re-run with --delete.")
        return

    # -D, not -d: a squash-merged branch is not an ancestor of master, so -d refuses every
    # one of them. The pull request state above is what stands in for that check.
    for name, _, _, _ in doomed:
        git("branch", "-D", name)
    print(f"\ndeleted {len(doomed)} local branches.")

    if args.remote:
        remote = {line.strip()[len("origin/"):] for line in git("for-each-ref", "--format=%(refname:short)", "refs/remotes/origin").splitlines()}
        stale = [n for n, _, _, _ in doomed if n in remote]
        for name in stale:
            git("push", "origin", "--delete", name)
        print(f"deleted {len(stale)} branches on origin." if stale else "origin carried none of them.")


if __name__ == "__main__":
    main()
