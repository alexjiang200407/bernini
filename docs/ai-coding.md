# AI Coding Bots

The agent flows that work on this repo — grilling a request, implementing it, the pre-PR read,
opening and watching pull requests, answering review — **live in the bernini-workspace repo**, not
here. They run a feature across several repositories at once (the engine, the test project, a
game), which a copy inside any one of them could not. What bernini carries is
[`.bcp/profile.md`](../.bcp/profile.md): YAML front matter the workspace's scripts read — the build,
test, format and tidy commands, how a release merges it, which flows it takes part in — and prose
its skills read: the `core` table, the Guiding Constraints lens, the asset-scale lenses, what a
review must never flag, and when a PR owes a Windows box. A plain clone still runs `just init`,
`just build`, `just test` and `just format`; it has no agent flows.

When an AI assistant acts on a pull request, whatever it posts goes up under whichever account `gh`
is logged in as — by default the developer's own, which reads as if they typed it by hand. A
**GitHub App**, `morgana-coding-agent` (App ID `4304152`), gives that work its own identity. An App
is a first-class GitHub actor: it consumes no collaborator seat, and its permission is scoped to
exactly what it needs — **Pull requests: Read and write**, since GitHub has no comment-only scope.

It replies to review comments and co-authors commits, from the workspace's PR tooling on a
developer's machine. Each developer holds **their own private key** under `~/.claude/`, so a leaked
key is revoked for that one person. A GitHub App cannot post directly: it signs a short-lived **JWT**
with its key and exchanges it for a one-hour **installation access token** (`ghs_…`) scoped to the
repo, which is what `gh` uses. Key setup and token minting belong to the workspace's PR tooling.

## Commit attribution

An AI-assisted commit stays **authored by the developer who ran it** and is **co-authored by the
bot** — the human is accountable for it, the bot is credited for the work. Claude Code exports
`CLAUDECODE=1` into every command it runs; Codex exports `CODEX_THREAD_ID`. The committed hook
[`.githooks/prepare-commit-msg`](../.githooks/prepare-commit-msg) keys off either rather than off a
trailer the assistant has to remember to write. It also replaces any `Co-authored-by: Claude …` line
the assistant did stamp, so the credit is the bot's either way:

```
Co-authored-by: morgana-coding-agent[bot] <305433938+morgana-coding-agent[bot]@users.noreply.github.com>
```

A commit made from your own shell has neither session variable nor a trailer, so it is left untouched and the
bot is credited only where it actually did the work. GitHub matches co-authors by that no-reply email
— `<user-id>+<login>@users.noreply.github.com`, where the user id (`305433938`) is stable across App
renames. The hook is idempotent (an amend does not duplicate the trailer), keeps the trailer out of
the commented help text git appends, and skips merge commits. `git commit --no-verify` would skip it
altogether, which is why the workspace's PR guard blocks that flag. It runs only because
`just init` sets `core.hooksPath` to `.githooks`; that is machine-local config, not committed, so a
fresh clone opts in through `just init` (or `git config core.hooksPath .githooks`).

That redirect is why the four Git LFS hooks (`post-checkout`, `post-commit`, `post-merge`,
`pre-push`) are committed alongside it. `git lfs install` writes them to whatever `core.hooksPath`
names, which on a fresh clone is `.git/hooks` — so pointing it at `.githooks` orphans them, and
`pre-push`, the hook that uploads LFS objects, stops running. A push would then publish pointer files
whose objects were never uploaded. Committing them keeps the redirect and the hooks together. Do not
delete them as regenerable cruft.

## First-time project setup (maintainer, once)

Already done for `bernini`: App ID `4304152`. These steps exist so the App can be recreated or
audited, and are **not** repeated per developer.

### 1. Register the App

GitHub → **Settings** → **Developer settings** → **GitHub Apps** → **New GitHub App**.

- **Name**: the name becomes the `…[bot]` login and the keys-page URL slug.
- **Homepage URL**: the repo URL. **Callback URL**, **Setup URL** and **Webhook URL**: leave blank.
- **Webhook**: uncheck **Active** — the bot only makes API calls and receives no events.
- **Repository permissions → Pull requests: Read and write** — to post replies and comments.
  Leave everything else **No access**. *(Without a repository permission, GitHub will not let the App
  be granted access to any repo — the installation shows "No repositories".)*
- **Where can this app be installed?**: **Only on this account**.

Create it, then note the numeric **App ID**; the workspace's PR tooling records it. Upload the App's
avatar from its settings page — it cannot be set from the registration form. The **Client ID** shown on the same page is for OAuth user-authorization flows and is unused
here.

Renaming an App later changes its slug and login but keeps the App ID and the bot's user id, so only
the URLs and display names in this repo need updating — the co-author email stays valid.

### 2. Install the App on the repo

On the App's settings page, **Install App** → install on the account → **Only select repositories**
→ pick **bernini**. This creates the installation the tokens are scoped to. If you later change the
App's permissions, GitHub holds them as *pending* until you approve them on this installation.

### 3. Keys

Nothing to distribute. Each developer generates their own key on the App's keys page and gives it
to the workspace's setup.

## Revoking access

- **Remove one developer**: delete their public key on
  <https://github.com/settings/apps/morgana-coding-agent/keys>. Their `.pem` stops working
  immediately; every other developer's key keeps working.
- **Cut the bot off entirely**: **Suspend** or **Uninstall** its installation on the repo. That
  disables every key at once.

## Security notes

- **A private key is a credential.** Anyone with one can act as the bot on the repo. Keep it in
  `~/.claude/` (outside the repo tree) and never commit it. Each dev's key is theirs alone.
- **Tokens are short-lived by design.** A fresh one-hour installation token is minted per use and
  never written to disk; there is nothing to rotate or revoke at that level.
- **Scope is minimal.** The App can only read and write pull requests on the one repo it is
  installed on. It cannot push code, change settings, or touch other repositories.
- **The committed files hold no secrets.** The App ID is an identifier; the private keys and
  `morgana-coding-agent.env` live only under `~/.claude/`.
