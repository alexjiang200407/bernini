# AI Coding Bots

When an AI assistant acts on a pull request, whatever it posts goes up under whichever account `gh`
is logged in as. By default that is the developer's own account, so machine-written comments show
their name and avatar, which reads as if they typed them by hand.

Two **GitHub Apps** give that work its own identity. An App is a first-class GitHub actor — it does
not consume a collaborator seat, its permissions are scoped to exactly what it needs, and it
sidesteps the "one machine user per human" gray area of a second personal account.

| | Coding agent | Review agent |
| --- | --- | --- |
| App ID | `4304152` (`morgana-coding-agent`) | `4314134` |
| Does | opens pull requests, applies review feedback with [`bcp-revise`](../.claude/skills/bcp-revise/SKILL.md), replies to each comment, co-authors commits | reviews a pull request when tagged |
| Runs | locally, on a developer's machine | on a GitHub Actions runner |
| Triggered by | a developer running `bcp-revise` | commenting `/review` on a pull request |
| Key lives in | `~/.claude/`, **one per developer** | the repository's Actions secrets, **one shared key** |
| `just init` | prompts for the key | not involved |

They are separate Apps because they need separate avatars, and that separation turns out to buy
something else: the shared-key model that a server-side agent forces is confined to the reviewer,
while the coding agent keeps per-developer keys and per-developer revocation.

Both hold the same permission — **Pull requests: Read and write**. GitHub has no comment-only
pull-request scope, so a reviewer cannot be granted anything narrower than a reviser.

## How App authentication works

A GitHub App cannot post directly; it authenticates in two hops:

1. Sign a short-lived **JWT** with a private key (RS256).
2. Exchange the JWT for an **installation access token** scoped to the repo. That token (`ghs_…`) is
   what `gh` uses, and it expires after one hour.

The coding agent does both hops with
[`mint-bot-token.sh`](../.claude/skills/bcp-revise/mint-bot-token.sh), which needs only `openssl` and
`curl` — both ship in Git Bash, so there are no extra dependencies — and prints the token.
[`scripts/pr.py`](../scripts/pr.py) runs it for every write it makes, so nothing has to remember to
export `GH_TOKEN`. The review agent does the same exchange with
[`actions/create-github-app-token`](https://github.com/actions/create-github-app-token), which avoids
writing a `.pem` to the runner's disk.

Each App is registered and installed **once** for the whole project (below). After that, each
developer who runs `bcp-revise` sets up their **own private key** for the coding agent — one App,
many keys — so a leaked key is revoked for that one person without disturbing anyone else. The review
agent has a single key, held only as a repository secret; nobody sets it up locally.

## Coding agent: per-developer setup

`bcp-revise` reads reviews and posts replies with the **GitHub CLI** (`gh`). It is not a pip package
— the `gh` on PyPI is unrelated — so install it from <https://cli.github.com/> and add it to PATH.
`just init` reports whether it is found.

The bot identity is all a developer needs beyond that, and `just init` handles it:

```bash
just init          # among the other prompts: "path to the bot private key .pem"
```

When it asks for the key, and you don't have one yet:

1. Open the App's keys page: <https://github.com/settings/apps/morgana-coding-agent/keys>
2. **Generate a private key** — a `.pem` downloads.
3. Give `init` the path to that file. It copies the key to
   `~/.claude/morgana-coding-agent.private-key.pem` (outside the repo, never committed) and writes
   `~/.claude/morgana-coding-agent.env` with the App ID.

`just init` also points git at the committed hooks (`core.hooksPath = .githooks`) so the
commit-attribution hook below is active.

That redirect is why the four Git LFS hooks (`post-checkout`, `post-commit`, `post-merge`,
`pre-push`) are committed alongside it. `git lfs install` writes them to whatever `core.hooksPath`
names, which on a fresh clone is `.git/hooks` — so pointing it at `.githooks` orphans them, and
`pre-push`, the hook that uploads LFS objects, stops running. A push would then publish pointer files
whose objects were never uploaded. Committing them keeps the redirect and the hooks together. Do not
delete them as regenerable cruft.

A blank answer skips the key — do that if you never run `bcp-revise`. Without it `just pr` refuses to
post rather than quietly using your account; `--as-me` is the deliberate override. Re-run `just init`
any time to set it up later.

Verify it works:

```bash
GH_TOKEN=$(just pr token) gh api installation/repositories --jq '.repositories[].full_name'
```

It should print `alexjiang200407/bernini`. An installation token authenticates as the App rather than
as a person, so `gh api user` is *not* the check — it 403s even when everything is correct.

## The enforced path: `just pr`

Which account a comment lands under, and whether it lands in the reviewer's thread or at the bottom
of the conversation, are decisions with one right answer every time. They are therefore not left to
the agent. [`scripts/pr.py`](../scripts/pr.py) is the only way it writes to a pull request:

| | | acts as |
| --- | --- | --- |
| `just pr create --base B --body-file F` | opens the PR, appends the diff breakdown, and arms the pending-watch list | **you** |
| `just pr edit <n> --body-file F` | rewrites the title or body, and refreshes the breakdown | **you** |
| `just pr edit <n>` | refreshes the breakdown alone — what a revision's push wants | **you** |
| `just pr comments <n>` | every review, thread and comment, each with the id to answer and whether it has been answered | — |
| `just pr reply <comment-id> --body-file F` | answers **where the comment was made** — the id is looked up, and an inline comment gets a threaded reply | **the bot** |
| `just pr comment <n> --body-file F` | a top-level summary; refuses while any thread is still unanswered | **the bot** |
| `just pr check <n>` | author, and whether anything is unanswered; exits 2 on a problem | — |

### The diff breakdown

`create` and `edit` write a table of the diff by category — production, shaders, tests, docs,
tooling, assets — into the body, between `<!-- bernini:diff -->` markers they rewrite in place.
`scripts/util/gitdiff.py` computes it, from `git diff --numstat -M` against the PR's base as origin
sees it; the same file already answers what a path is for the formatter and the coverage report, so
there is one classifier rather than three.

It is generated rather than asked of the agent because a hand-written table is stale the moment a
revision pushes, and forgotten before that. Prose written above or below the markers is left alone,
so a body is safe to edit by hand.

The title heads the body file as `# one-line title` and is lifted out of the body. It travels in the
file because `just` joins a recipe's arguments on spaces, so a `--title` containing one cannot survive
the trip; the flag still works when `scripts/pr.py` is called directly.

### Why the bot comments but does not open

A comment is the agent speaking, and it should not wear the developer's face. A **pull request is
different**, because GitHub takes a squash-merged commit's author from the *pull request's* author —
not from the commits on the branch, which it discards. So a bot-authored PR does not merely look
machine-made: it signs every line it lands on `master` as the bot's, and `git blame`, `git shortlog`
and the repository's contributor list all follow the author. `bcp-feature` squashes twice — task PR
into `feat/<name>`, then the feature into `master` — so the identity is rewritten a level down, where
no later rebase can recover it.

`just pr check` therefore treats a bot-authored PR as a **problem**, and says to merge that one with
rebase rather than squash. `--as-bot` and `--as-me` override either default when you mean to.

The `Co-authored-by` trailer is what credits the bot instead, and it costs the developer nothing:
GitHub's contributor statistics count a co-author's additions in full, alongside the author's, rather
than splitting them.

Two of the Claude Code hooks in [`.claude/settings.json`](../.claude/settings.json) close the gaps
around it:

- [`gh_guard.py`](../.claude/hooks/gh_guard.py) (`PreToolUse`) rejects `gh pr create`, `gh pr comment`,
  `gh pr review`, `gh pr merge`, a `gh api` write to a comment endpoint, and `git commit --no-verify`,
  naming the `just pr` command to use instead. Reads — `gh pr view`, `gh api … --jq` — are untouched.
- [`pr_watch_guard.py`](../.claude/hooks/pr_watch_guard.py) (`Stop`) refuses to end a turn while a PR
  this session wrote to has no watcher. `pr.py` arms the list; [`watch_pr.py`](../scripts/watch_pr.py)
  claims the PR as it starts, refuses to start where another watcher already holds it, re-arms it on a
  review, a comment, a red build or a moved base — which is exactly when the PR is waiting on the
  agent again — and drops it for good once the PR merges or closes. `just pr unwatch <n>` releases one deliberately.

The watcher claims the PR at startup rather than at exit so that it can run **in the background**: the
hook is satisfied by a watch that is *running*, the turn ends, and the developer keeps their session
instead of watching a blocked prompt until the PR moves.

**One watcher per PR.** The claim is the watcher's pid, so the list answers *is anyone watching this*
rather than *has anyone ever watched it*: a PR with a live watcher is not pending however many times
it is armed again, and `just watch-pr` on it says who holds it and exits instead of starting a second.
A claim recording only that someone had started would let the halves fight — every `pr.py` write arms,
so a reply posted mid-watch blocks the turn, the agent starts another watcher, and each duplicate then
wakes it for the same comment.

The armed entry is left in place for as long as the watch runs rather than being removed by it, which
is what makes a watcher's death safe: liveness is decided on every read, so a claim whose process is
gone stops counting and the PR is pending again. Disarming at startup instead would lose the PR
outright whenever a watcher exits before reporting anything — a failed first poll, a timeout, a
`kill` — leaving it neither watched nor pending, which is the silence the hook exists to prevent.

Each entry also carries the timestamp of the last thing posted, which is what `just watch-pr` uses as
its baseline when `--since` is not given. A hand-written timestamp two seconds early makes the watcher
fire on the agent's own reply and spend the turn reading itself, so the time comes from GitHub's
response rather than from anyone's judgement.

The list lives in the worktree's git dir as `bernini-pr-watch.json` (`.git/`, or
`.git/worktrees/<name>/` in a linked worktree), under two keys: `pending`, keyed by session so a later
session never inherits a block for a PR it knows nothing about, and `watching`, keyed by pid and
deliberately not by session — a watcher started by one session still counts for another, because the
PR is being watched either way.

A third hook in the same file, [`ask_guard.py`](../.claude/hooks/ask_guard.py) (`PreToolUse`), is
not about identity at all: it holds a `ws ask` session to reading, and is inert unless `WS_ASK` is
set. Two rules — one allowlist of paths that may be written, and no shell at all, Bash being refused
outright rather than inspected. [`bcp-ask`](../.claude/skills/bcp-ask/SKILL.md) is where it is
described; `scripts/tests/test_ask_guard.py` pins what it refuses, and carries the record of why
inspecting a command line was abandoned.

A fourth, [`draft_commit.py`](../.claude/hooks/draft_commit.py) (`PostToolUse`), is that guard's
other half: it commits any write landing under `docs/specs/`, the worktree of the orphan
`spec-drafts` branch every checkout links to — specs are not documentation and are not on master, so
that directory is the link and nothing else. It matches the shell as well as the editing tools,
because a `sed -i` names no file the hook could read — that path ignores the tool input and commits
whatever the worktree turns out to be holding, behind a read-only `status` so the commands that
touch no draft pay one cheap call and stop. An ask session cannot commit its own draft because it
has no shell, and a rule saying somebody should commit it afterwards is what left an untracked spec
to be deleted by a `git pull` on 2026-09-01. Unlike `ask_guard.py` it runs in every session, not only
under `WS_ASK` — a feature agent revising a draft has the same window — and unlike it, it fails
*open*: the write has already happened when the hook runs, so a crash here has only failed to commit
a file, where a crash in a guard would have allowed a write. It finds the worktree by resolving the
checkout's own symlink and is inert wherever there is none, which is CI and every fresh clone.
`scripts/tests/test_draft_commit.py` pins it.

## Coding agent: commit attribution

An AI-assisted commit stays **authored by the developer who ran it** and is **co-authored by the
bot** — the human is accountable for it, the bot is credited for the work. Claude Code exports
`CLAUDECODE=1` into every command it runs, and the committed hook
[`.githooks/prepare-commit-msg`](../.githooks/prepare-commit-msg) keys off that rather than off a
trailer the assistant has to remember to write. It also replaces any `Co-authored-by: Claude …` line
the assistant did stamp, so the credit is the bot's either way:

```
Co-authored-by: morgana-coding-agent[bot] <305433938+morgana-coding-agent[bot]@users.noreply.github.com>
```

A commit made from your own shell has no `CLAUDECODE` and no trailer, so it is left untouched and the
bot is credited only where it actually did the work. GitHub matches co-authors by that no-reply email
— `<user-id>+<login>@users.noreply.github.com`, where the user id (`305433938`) is stable across App
renames. The hook is idempotent (an amend does not duplicate the trailer), keeps the trailer out of
the commented help text git appends, and skips merge commits. `git commit --no-verify` would skip it
altogether, which is why `gh_guard.py` blocks that flag. It runs only because
`just init` sets `core.hooksPath` to `.githooks`; that is machine-local config, not committed, so a
fresh clone opts in through `just init` (or `git config core.hooksPath .githooks`).

The review agent never commits, so no trailer applies to it.

## Review agent: comment to review

Open a pull request comment with `/review` and
[`.github/workflows/review.yml`](../.github/workflows/review.yml) reviews it. No machine of yours is
involved, so a review can be asked for from anywhere and answered whenever.

**It is asked for, never automatic.** Every pull request has already been read once before it opened,
by [`bcp-precheck`](../.claude/agents/bcp-precheck.md) on the machine that wrote it, so the routine
findings are gone before anyone sees the diff. Spending a run on each pull request would mostly pay
to rediscover them. `/review` is for the change where a second, independent read is worth it.

The trigger is a bare slash command rather than an `@`-mention on purpose: GitHub resolves a mention
against real accounts, so any username-shaped trigger notifies whoever holds that name — or whoever
registers it later. `/review` can never address a person. It is matched with `startsWith`, so
trailing instructions are fine (`/review focus on the Metal backend`) but a comment that merely links
to or quotes the command does not spend a run.

The workflow triggers on `issue_comment` — GitHub models pull-request comments as issue comments —
and three properties of that trigger are load-bearing:

- **It always runs the workflow file from the default branch**, never the version on the pull
  request's branch. A pull request therefore cannot edit the workflow to reach the secrets. This is
  also why a change to `review.yml` does nothing until it lands on `master`: you cannot test it from
  a branch.
- **The job is gated on `author_association`** (`OWNER`, `MEMBER`, `COLLABORATOR`). Without that
  gate, anyone who can comment can spend the subscription's quota.
- **The reviewer reads the diff; it never runs the pull request's code.** Keeping it that way is what
  makes it safe to hold secrets in a job that is looking at untrusted input.

### Secrets

Two repository secrets, at Settings → Secrets and variables → Actions:

| Secret | Value |
| --- | --- |
| `MAKOTO_REVIEW_KEY` | the review App's private key — the whole `.pem`, `BEGIN`/`END` lines included |
| `CLAUDE_CODE_OAUTH_TOKEN` | output of `claude setup-token`, run locally |

The App ID is inline in the workflow, not a secret — it is an identifier, the same way `BOT_APP_ID`
is hardcoded in [`scripts/init.py`](../scripts/init.py).

`CLAUDE_CODE_OAUTH_TOKEN` bills the **Claude Max subscription** rather than the API. A Max plan does
not include API access — Console and API are billed separately — so this token is what lets the
reviewer run without an API key. Two consequences follow. Reviews draw on the same quota as your own
interactive Claude Code sessions, so a large review competes with your own headroom. And the token
expires and must be rotated by re-running `claude setup-token` and updating the secret; nothing warns
you first, so a reviewer that has gone quiet is worth checking here.

Never set `ANTHROPIC_API_KEY` alongside it. A static API credential silently takes precedence and
bills per token.

## First-time project setup (maintainer, once per App)

Already done for `bernini`: the coding agent is App ID `4304152`, the review agent `4314134`. These
steps exist so an App can be recreated or audited, and are **not** repeated per developer.

### 1. Register the App

GitHub → **Settings** → **Developer settings** → **GitHub Apps** → **New GitHub App**.

- **Name**: the name becomes the `…[bot]` login and the keys-page URL slug.
- **Homepage URL**: the repo URL. **Callback URL**, **Setup URL** and **Webhook URL**: leave blank.
- **Webhook**: uncheck **Active** — the bots only make API calls, they receive no events. This holds
  for the review agent too: the `issue_comment` subscription belongs to GitHub Actions, not the App.
- **Repository permissions → Pull requests: Read and write** — to post reviews, replies and comments.
  Leave everything else **No access**. *(Without a repository permission, GitHub will not let the App
  be granted access to any repo — the installation shows "No repositories".)*
- **Where can this app be installed?**: **Only on this account**.

Create it, then note the numeric **App ID**. For the coding agent it is set as `BOT_APP_ID` in
[`scripts/init.py`](../scripts/init.py) so `just init` writes it for everyone; for the review agent it
is inline in `review.yml`. Upload the App's avatar from its settings page — that is what
distinguishes the two bots in a pull request's timeline, and it cannot be set from the registration
form. The **Client ID** shown on the same page is for OAuth user-authorization flows and is unused
here.

Renaming an App later changes its slug and login but keeps the App ID and the bot's user id, so only
the URLs and display names in this repo need updating — the co-author email stays valid.

### 2. Install the App on the repo

On the App's settings page, **Install App** → install on the account → **Only select repositories**
→ pick **bernini**. This creates the installation the tokens are scoped to. If you later change the
App's permissions, GitHub holds them as *pending* until you approve them on this installation.

### 3. Distribute the key

- **Coding agent**: nothing to distribute. Each developer generates their own key through
  `just init`.
- **Review agent**: generate one key and paste the `.pem` into the `MAKOTO_REVIEW_KEY` repository
  secret. GitHub shows the key once and never again, so do this before losing the file.

## Revoking access

The two Apps revoke differently, and the difference is the reason they are two Apps:

- **Remove one developer from the coding agent**: delete their public key on
  <https://github.com/settings/apps/morgana-coding-agent/keys>. Their `.pem` stops working
  immediately; every other developer's key keeps working.
- **Cut the review agent off**: delete the `MAKOTO_REVIEW_KEY` secret, or rotate the App's key. There
  is only one key, so this stops the reviewer for everyone at once — there is no per-person
  revocation for a credential that belongs to no person.
- **Cut a bot off entirely**: **Suspend** or **Uninstall** its installation on the repo. That
  disables all of that App's keys at once.

## Security notes

- **A private key is a credential.** Anyone with one can act as that bot on the repo. Keep the coding
  agent's in `~/.claude/` (outside the repo tree) and never commit it. Each dev's key is theirs alone.
- **The review agent's key is shared and lives in Actions secrets.** That is the cost of running on a
  server rather than a laptop, and it is contained to the reviewer by design.
- **Tokens are short-lived by design.** Both paths mint a fresh one-hour installation token per run
  and never write it to disk; there is nothing to rotate or revoke at that level. The exception is
  `CLAUDE_CODE_OAUTH_TOKEN`, which is long-lived and is a Claude credential rather than a GitHub one.
- **Scope is minimal.** Each App can only read and write pull requests on the one repo it is
  installed on. Neither can push code, change settings, or touch other repositories.
- **The committed files hold no secrets.** `mint-bot-token.sh`, the `BOT_APP_ID` in `init.py`, and the
  App ID in `review.yml` only identify the Apps and read their keys at runtime. An App ID is an
  identifier, not a secret; the private keys and `morgana-coding-agent.env` live only under
  `~/.claude/` or in the repository's Actions secrets.
