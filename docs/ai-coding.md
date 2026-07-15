# AI Coding Bot

When an AI assistant acts on a pull request — applying review feedback with
[`bcp-revise`](../.claude/skills/bcp-revise/SKILL.md), then replying to each comment on GitHub — the
replies post under whichever account `gh` is logged in as. By default that is the developer's own
account, so machine-written review replies show their name and avatar, which reads as if they typed
them by hand.

The **`morgana-coding-agent` GitHub App** gives that work its own identity: review replies post as
`morgana-coding-agent[bot]` with the `bot` badge, and AI-assisted commits are co-authored by the
same bot (below). The App is a first-class GitHub actor — it does not consume a collaborator seat,
its permissions are scoped to exactly what it needs, and it sidesteps the "one machine user per
human" gray area of a second personal account.

## How it works

A GitHub App cannot post directly; it authenticates in two hops:

1. Sign a short-lived **JWT** with a private key (RS256).
2. Exchange the JWT for an **installation access token** scoped to the repo. That token (`ghs_…`) is
   what `gh` uses, and it expires after one hour.

[`mint-bot-token.sh`](../.claude/skills/bcp-revise/mint-bot-token.sh) does both hops with `openssl`
and `curl` — both ship in Git Bash, so there are no extra dependencies — and prints the token.
`bcp-revise` exports it as `GH_TOKEN` for the reply step, and falls back to the logged-in account
when the bot is not set up on this machine.

The App is registered and installed **once** for the whole project (below). After that, each
developer who runs `bcp-revise` sets up their **own private key** — one App, many keys — so a leaked
key is revoked for that one person without disturbing anyone else.

## Per-developer setup

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

A blank answer skips the key — do that if you never run `bcp-revise`; your replies just post as your
own account. Re-run `just init` any time to set it up later.

Verify it works:

```bash
GH_TOKEN=$(bash .claude/skills/bcp-revise/mint-bot-token.sh) gh api user --jq .login
```

It should print `morgana-coding-agent[bot]`. If it prints your own login, the mint failed and `gh`
fell back to your account — run `mint-bot-token.sh` alone to see the error on stderr.

## Commit attribution

AI-assisted commits are co-authored by the bot rather than the assistant tool. The assistant already
stamps its own `Co-authored-by: Claude …` trailer when it writes a commit; the committed hook
[`.githooks/prepare-commit-msg`](../.githooks/prepare-commit-msg) swaps that line for the bot:

```
Co-authored-by: morgana-coding-agent[bot] <305433938+morgana-coding-agent[bot]@users.noreply.github.com>
```

A commit with no assistant trailer — a purely human one — is left untouched, so the bot is credited
only where it actually did the work. GitHub matches co-authors by that no-reply email —
`<user-id>+<login>@users.noreply.github.com`, where the user id (`305433938`) is stable across App
renames. The hook is idempotent (an amend does not duplicate the trailer). It runs only because
`just init` sets `core.hooksPath` to `.githooks`; that is machine-local config, not committed, so a
fresh clone opts in through `just init` (or `git config core.hooksPath .githooks`).

## First-time project setup (maintainer, once)

Already done for `bernini` (App ID `4304152`). These steps exist so the App can be recreated or
audited, and are **not** repeated per developer.

### 1. Register the App

GitHub → **Settings** → **Developer settings** → **GitHub Apps** → **New GitHub App**.

- **Name**: `morgana-coding-agent` (this becomes the `morgana-coding-agent[bot]` login and the
  keys-page URL slug).
- **Homepage URL**: the repo URL. **Callback URL** and **Webhook URL**: leave blank.
- **Webhook**: uncheck **Active** — the bot only makes API calls, it receives no events.
- **Repository permissions → Pull requests: Read and write** — to post review replies and comments.
  Leave everything else **No access**. *(Without a repository permission, GitHub will not let the App
  be granted access to any repo — the installation shows "No repositories".)*
- **Where can this app be installed?**: **Only on this account**.

Create it, then note the numeric **App ID** and set it as `BOT_APP_ID` in
[`scripts/init.py`](../scripts/init.py) so `just init` writes it for everyone. Renaming the App later
changes its slug and login but keeps the App ID and the bot's user id, so only the URLs and
display names in this repo need updating — the co-author email stays valid.

### 2. Install the App on the repo

On the App's settings page, **Install App** → install on the account → **Only select repositories**
→ pick **bernini**. This creates the installation the tokens are scoped to. If you later change the
App's permissions, GitHub holds them as *pending* until you approve them on this installation.

## Revoking access

Because each developer holds their own key, access is removed per person without rotating anything
for the rest of the team:

- **Remove one developer**: delete their public key on
  <https://github.com/settings/apps/morgana-coding-agent/keys>. Their `.pem` stops working
  immediately; every other key keeps working.
- **Cut the bot off entirely**: **Suspend** or **Uninstall** the installation on the repo. That
  disables all keys at once.

## Security notes

- **A private key is a credential.** Anyone with one can act as the bot on the repo. Keep it in
  `~/.claude/` (outside the repo tree) and never commit it. Each dev's key is theirs alone.
- **Tokens are short-lived by design.** `mint-bot-token.sh` prints a fresh one-hour token each run
  and never writes it to disk; there is nothing to rotate or revoke at the token level.
- **Scope is minimal.** The App can only read and write pull requests on the one repo it is installed
  on. It cannot push code, change settings, or touch other repositories.
- **The committed files hold no secrets.** `mint-bot-token.sh` and the `BOT_APP_ID` in `init.py` only
  identify the App and read the key from `~/.claude/` at runtime. The App ID is an identifier, not a
  secret; the private keys and `morgana-coding-agent.env` live only under `~/.claude/`.
