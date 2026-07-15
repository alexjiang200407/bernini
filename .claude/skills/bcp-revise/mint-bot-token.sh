#!/usr/bin/env bash
# Mint a short-lived GitHub App installation token for the morgana-coding-agent bot
# and print it (a `ghs_...` token) to stdout. Nothing else goes to stdout, so callers
# can do
#     GH_TOKEN=$(bash .claude/skills/bcp-revise/mint-bot-token.sh)
# and use it for `gh api` / `gh pr comment`. The token is valid for one hour.
#
# Needs only openssl and curl (both ship in Git Bash). Reads its settings from
# ~/.claude/morgana-coding-agent.env (override with $MORGANA_ENV):
#     MORGANA_APP_ID           App ID (GitHub -> Settings -> Developer settings -> GitHub Apps)
#     MORGANA_KEY              path to the App private key .pem
#     MORGANA_INSTALLATION_ID  optional; looked up from the repo when unset
#     MORGANA_REPO             optional owner/repo; defaults to the origin remote
#
# Exits non-zero (and explains on stderr) when it cannot mint a token, which lets
# the caller fall back to the logged-in account. See docs/ai-coding.md.
set -euo pipefail

cfg="${MORGANA_ENV:-$HOME/.claude/morgana-coding-agent.env}"
# shellcheck disable=SC1090
[ -f "$cfg" ] && . "$cfg"

: "${MORGANA_APP_ID:?set MORGANA_APP_ID in $cfg}"
: "${MORGANA_KEY:?set MORGANA_KEY (path to the .pem) in $cfg}"
key="${MORGANA_KEY/#\~/$HOME}"
[ -f "$key" ] || { echo "bot private key not found: $key" >&2; exit 1; }

b64url() { openssl base64 -A | tr '+/' '-_' | tr -d '='; }

now=$(date +%s)
header=$(printf '{"alg":"RS256","typ":"JWT"}' | b64url)
payload=$(printf '{"iat":%d,"exp":%d,"iss":"%s"}' "$((now - 60))" "$((now + 540))" "$MORGANA_APP_ID" | b64url)
unsigned="$header.$payload"
sig=$(printf '%s' "$unsigned" | openssl dgst -sha256 -sign "$key" -binary | b64url)
jwt="$unsigned.$sig"

api() {
  curl -sSf \
    -H "Authorization: Bearer $jwt" \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" "$@"
}

# GitHub returns pretty-printed JSON ("id": 123, with a space), so the patterns
# tolerate whitespace after the colon rather than assuming a compact body.
inst="${MORGANA_INSTALLATION_ID:-}"
if [ -z "$inst" ]; then
  repo="${MORGANA_REPO:-$(git config --get remote.origin.url \
    | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')}"
  inst=$(api "https://api.github.com/repos/$repo/installation" \
    | grep -oE '"id":[[:space:]]*[0-9]+' | head -1 | grep -oE '[0-9]+')
fi
[ -n "$inst" ] || { echo "could not resolve the bot's installation id" >&2; exit 1; }

api -X POST "https://api.github.com/app/installations/$inst/access_tokens" \
  | grep -oE '"token":[[:space:]]*"[^"]+"' | head -1 | sed -E 's/.*"([^"]+)"$/\1/'
