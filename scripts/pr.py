#!/usr/bin/env python3
"""Every write to a pull request, made the way the project requires.

An agent asked to "answer each comment in its thread, as the bot" gets it right
most of the time, which is not the same as always. This script is the rule made
mechanical: it decides in-thread versus top-level by looking the comment up
rather than by being told, and it picks the actor by what is being written
rather than by being reminded.

The split is deliberate. A **comment** is the agent speaking, and goes up as the
morgana-coding-agent bot. The **pull request** is opened by the developer,
because GitHub rewrites a squash-merged commit's author to the pull request's
author -- a bot-authored PR would sign every line of `master` as the bot's, and
`bcp-feature` squashes a second time on the way in, so no later merge can undo
it. `--as-bot` and `--as-me` override either default.

    pr.py create --base master --body-file body.md   # '# title' heads the file
    pr.py comments 184                     # every review, thread and comment, with ids
    pr.py reply 2154783 --body "..."       # routed by what the id turns out to be
    pr.py comment 184 --body-file s.md     # top-level summary; refuses while a thread is open
    pr.py edit 184 --body-file body.md     # rewrite the title or body
    pr.py check 184                        # author, and whether anything is unanswered
    pr.py unwatch 184                      # drop it from the pending-watch list

`create` also arms the pending-watch list, which the Stop hook reads: the turn
that opens a PR cannot end without `watch_pr.py` running on it.

Exits 1 on any failure, 2 when `check` finds problems. See docs/ai-coding.md.
"""

import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from util import gh, watchlist  # noqa: E402


def read_body(args):
    """The body text from --body, --body-file, or --body-file - for stdin."""
    if args.body is not None:
        return args.body
    if args.body_file == "-":
        return sys.stdin.read()
    with open(args.body_file, encoding="utf-8") as fh:
        return fh.read()


def split_title(body):
    """A leading '# title' line lifted out of `body`, as (title, rest).

    `just` joins a recipe's arguments on spaces, so a --title that contains one
    cannot survive the trip. The title therefore travels in the file with the
    body, which needs no quoting at all.
    """
    lines = body.lstrip("\n").split("\n")
    if not lines or not lines[0].startswith("# "):
        return None, body
    rest = lines[1:]
    while rest and not rest[0].strip():
        rest.pop(0)
    return lines[0][2:].strip(), "\n".join(rest)


def add_body_args(parser, required=True):
    group = parser.add_mutually_exclusive_group(required=required)
    group.add_argument("--body", help="body text")
    group.add_argument("--body-file", help="file holding the body ('-' for stdin)")


def actor(args, bot_by_default):
    """The token to post with: the bot's, or None for the logged-in account.

    The two halves of a pull request want different actors. A **comment** is the
    agent talking, and posts as the bot. The **pull request itself** is authored
    by the developer, because a squash merge rewrites the resulting commit's
    author to the PR's author -- a bot-authored PR would put the bot's name on
    every line of `master`, and `bcp-feature` squashes a second time on the way
    in, so nothing downstream can recover the identity.
    """
    if args.as_bot and args.as_me:
        sys.exit("error: --as-me and --as-bot contradict each other")
    if args.as_me or (not bot_by_default and not args.as_bot):
        return None
    token = gh.bot_token()
    if not token:
        sys.exit(
            "error: could not mint a morgana-coding-agent token, so this would post under\n"
            "       your own account. Run `just init` to set the bot key up (docs/ai-coding.md),\n"
            "       or pass --as-me to post as yourself deliberately.")
    return token


def git(*args):
    return subprocess.run(
        ["git"] + list(args), capture_output=True, text=True).stdout.strip()


def cmd_create(args):
    slug = gh.repo_slug(args.repo)
    head = args.head or git("rev-parse", "--abbrev-ref", "HEAD")
    if head in ("master", "main"):
        sys.exit(f"error: refusing to open a PR from {head}; work belongs on a branch")
    if not git("ls-remote", "--heads", "origin", head):
        sys.exit(f"error: origin has no branch '{head}' -- push it first (git push -u origin HEAD)")

    heading, body = split_title(read_body(args))
    title = args.title or heading
    if not title:
        sys.exit("error: no title. Start the body file with a '# one-line title' heading,\n"
                 "       or pass --title (which needs `python scripts/pr.py`, since `just`\n"
                 "       joins a recipe's arguments on spaces).")

    token = actor(args, bot_by_default=False)
    fields = {"title": title, "head": head, "base": args.base, "body": body}
    try:
        pr = gh.api(f"repos/{slug}/pulls", token=token, method="POST", fields=fields,
                    raw_fields={"draft": "true" if args.draft else "false"})
    except gh.GhError as e:
        if "403" in str(e) or "Resource not accessible" in str(e):
            sys.exit(f"error: not allowed to open a pull request here ({e}).\n"
                     "       With --as-bot, grant the App 'Pull requests: Read and write' and\n"
                     "       approve the pending permission on its installation; otherwise check\n"
                     "       `gh auth status` -- see docs/ai-coding.md.")
        sys.exit(f"error: {e}")

    watchlist.arm(pr["number"], pr["html_url"], pr.get("created_at"))
    print(json.dumps({
        "number": pr["number"],
        "url": pr["html_url"],
        "author": (pr.get("user") or {}).get("login"),
        "base": args.base,
        "head": head,
    }, indent=2))
    print(f"\nnow watch it: just watch-pr {pr['number']}", file=sys.stderr)


def collect(slug, pr, token=None):
    """The PR plus its reviews, inline threads and top-level comments."""
    view = gh.api(f"repos/{slug}/pulls/{pr}", token=token)
    reviews = gh.api(f"repos/{slug}/pulls/{pr}/reviews", token=token) or []
    inline = gh.api(f"repos/{slug}/pulls/{pr}/comments?per_page=100", token=token) or []
    issue = gh.api(f"repos/{slug}/issues/{pr}/comments?per_page=100", token=token) or []

    author = ((view.get("user") or {}).get("login") or "").lower()
    ours = {gh.BOT_LOGIN.lower(), author}

    replies = {}
    for c in inline:
        root = c.get("in_reply_to_id")
        if root:
            replies.setdefault(root, []).append(c)

    threads = []
    for c in inline:
        if c.get("in_reply_to_id"):
            continue
        kids = replies.get(c["id"], [])
        threads.append({
            "id": c["id"],
            "author": (c.get("user") or {}).get("login"),
            "path": c.get("path"),
            "line": c.get("line") or c.get("original_line"),
            "body": c.get("body", ""),
            "url": c.get("html_url"),
            "answered": any((k.get("user") or {}).get("login", "").lower() in ours for k in kids),
            "replies": [{"author": (k.get("user") or {}).get("login"), "body": k.get("body", "")}
                        for k in kids],
        })

    return {
        "pr": pr,
        "url": view.get("html_url"),
        "state": view.get("state"),
        "author": (view.get("user") or {}).get("login"),
        "reviews": [{"id": r.get("id"),
                     "author": (r.get("user") or {}).get("login"),
                     "state": r.get("state"),
                     "body": r.get("body", "")}
                    for r in reviews if r.get("state") != "PENDING"],
        "threads": threads,
        "issue_comments": [{"id": c.get("id"),
                            "author": (c.get("user") or {}).get("login"),
                            "body": c.get("body", "")}
                           for c in issue],
    }


def cmd_comments(args):
    print(json.dumps(collect(gh.repo_slug(args.repo), args.pr), indent=2))


def cmd_reply(args):
    """Answers comment `id` wherever it lives -- the id decides, not the caller."""
    slug = gh.repo_slug(args.repo)
    token = actor(args, bot_by_default=True)
    body = read_body(args)

    try:
        target = gh.api(f"repos/{slug}/pulls/comments/{args.id}")
    except gh.GhError:
        target = None

    if target is not None:
        pr = int(target["pull_request_url"].rsplit("/", 1)[-1])
        root = target.get("in_reply_to_id") or target["id"]
        posted = gh.api(f"repos/{slug}/pulls/{pr}/comments/{root}/replies",
                        token=token, method="POST", fields={"body": body})
        watchlist.arm(pr, posted.get("html_url", ""), posted.get("created_at"))
        print(json.dumps({"kind": "thread-reply", "pr": pr, "thread": root,
                          "url": posted.get("html_url")}, indent=2))
        return

    try:
        issue = gh.api(f"repos/{slug}/issues/comments/{args.id}")
    except gh.GhError:
        sys.exit(f"error: {args.id} is not a comment on {slug}. If it came from the `reviews` list,\n"
                 "       it is a review summary -- GitHub gives those no thread to reply in, so the\n"
                 "       conversation is the right place: `just pr comment <n> --body-file <file>`.\n"
                 "       Otherwise list the ids again with `just pr comments <n>`.")

    pr = int(issue["issue_url"].rsplit("/", 1)[-1])
    posted = gh.api(f"repos/{slug}/issues/{pr}/comments", token=token, method="POST",
                    fields={"body": body})
    watchlist.arm(pr, posted.get("html_url", ""), posted.get("created_at"))
    print(json.dumps({"kind": "top-level", "pr": pr, "url": posted.get("html_url"),
                      "note": "that id is a conversation comment; GitHub has no thread to "
                              "reply in, so this went to the conversation"}, indent=2))


def unanswered(data):
    return [t for t in data["threads"] if not t["answered"]]


def cmd_comment(args):
    slug = gh.repo_slug(args.repo)
    data = collect(slug, args.pr)
    open_threads = unanswered(data)
    if open_threads and not args.force:
        lines = "\n".join(f"  {t['id']}  {t['path']}:{t['line']}  [{t['author']}]"
                          for t in open_threads)
        sys.exit(f"error: {len(open_threads)} review thread(s) have no reply. A summary in the\n"
                 f"       conversation leaves the question where it was asked:\n{lines}\n"
                 f"       Answer each with `just pr reply <id> --body-file <f>` first "
                 f"(--force overrides).")

    token = actor(args, bot_by_default=True)
    posted = gh.api(f"repos/{slug}/issues/{args.pr}/comments", token=token, method="POST",
                    fields={"body": read_body(args)})
    watchlist.arm(args.pr, posted.get("html_url", ""), posted.get("created_at"))
    print(json.dumps({"kind": "top-level", "pr": args.pr, "url": posted.get("html_url")}, indent=2))


def cmd_edit(args):
    slug = gh.repo_slug(args.repo)
    token = actor(args, bot_by_default=False)
    fields = {}
    if args.body or args.body_file:
        heading, body = split_title(read_body(args))
        fields["body"] = body
        if heading:
            fields["title"] = heading
    if args.title:
        fields["title"] = args.title
    if not fields:
        sys.exit("error: nothing to change; pass --title and/or a body")
    pr = gh.api(f"repos/{slug}/pulls/{args.pr}", token=token, method="PATCH", fields=fields)
    print(json.dumps({"pr": args.pr, "url": pr["html_url"]}, indent=2))


def cmd_check(args):
    slug = gh.repo_slug(args.repo)
    data = collect(slug, args.pr)
    problems = []
    if (data["author"] or "").lower() == gh.BOT_LOGIN.lower():
        problems.append(f"authored by {gh.BOT_LOGIN}: a squash merge would put the bot's name "
                        f"on every line it lands. Merge this one with rebase, and open the "
                        f"next with `just pr create` (which posts as you)")
    for t in unanswered(data):
        problems.append(f"thread {t['id']} ({t['path']}:{t['line']}, {t['author']}) has no reply")

    for p in problems:
        print(f"  x {p}")
    if problems:
        print(f"{len(problems)} problem(s)")
        sys.exit(2)
    print(f"PR #{args.pr}: authored by {data['author']}, "
          f"{len(data['threads'])} thread(s) all answered")


def cmd_unwatch(args):
    watchlist.disarm(args.pr)
    print(f"PR #{args.pr} no longer requires a watcher this turn")


def cmd_token(_args):
    token = gh.bot_token()
    if not token:
        sys.exit("error: no bot token (run `just init`; see docs/ai-coding.md)")
    print(token)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)

    # On the subcommands rather than ahead of them: `just pr create ... --as-bot` is
    # the order anyone writes, and argparse rejects a top-level flag placed there.
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--repo", help="owner/name (default: the repo of the cwd)")
    common.add_argument("--as-me", action="store_true",
                        help="act as the logged-in account (the default for create/edit)")
    common.add_argument("--as-bot", action="store_true",
                        help="act as the bot (the default for reply/comment)")

    subs = parser.add_subparsers(dest="cmd", required=True)

    def add(name, **kw):
        return subs.add_parser(name, parents=[common], **kw)

    p = add("create", help="open a PR from the current branch, authored by you")
    p.add_argument("--base", required=True, help="branch to merge into")
    p.add_argument("--title", help="default: the body's leading '# title' heading")
    p.add_argument("--head", help="branch to merge from (default: the current one)")
    p.add_argument("--draft", action="store_true")
    add_body_args(p)
    p.set_defaults(func=cmd_create)

    p = add("comments", help="every review, thread and comment, with ids")
    p.add_argument("pr", type=int)
    p.set_defaults(func=cmd_comments)

    p = add("reply", help="answer a comment where it was made")
    p.add_argument("id", type=int, help="comment id from `pr.py comments`")
    add_body_args(p)
    p.set_defaults(func=cmd_reply)

    p = add("comment", help="top-level summary, once every thread is answered")
    p.add_argument("pr", type=int)
    p.add_argument("--force", action="store_true", help="post with threads still unanswered")
    add_body_args(p)
    p.set_defaults(func=cmd_comment)

    p = add("edit", help="rewrite the title or body")
    p.add_argument("pr", type=int)
    p.add_argument("--title")
    add_body_args(p, required=False)
    p.set_defaults(func=cmd_edit)

    p = add("check", help="author and unanswered threads; exits 2 on problems")
    p.add_argument("pr", type=int)
    p.set_defaults(func=cmd_check)

    p = add("unwatch", help="drop a PR from the pending-watch list")
    p.add_argument("pr", type=int)
    p.set_defaults(func=cmd_unwatch)

    add("token", help="print a one-hour bot token").set_defaults(func=cmd_token)

    args = parser.parse_args()
    try:
        args.func(args)
    except gh.GhError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
