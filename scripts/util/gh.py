"""GitHub CLI access, and the morgana-coding-agent identity the AI posts under.

Everything that writes to a pull request goes through here so that "post as the
bot, not as the developer" is one decision made in one place rather than a rule
each caller is trusted to remember. `bot_token` mints a one-hour installation
token with mint-bot-token.sh; `run_gh` runs `gh` with it exported, so the actor
is whoever `token` says and never the ambient login by accident.

See docs/ai-coding.md for the App itself.
"""

import json
import os
import shutil
import subprocess
import sys

from . import cmake_tools as ct

# The App's bot account, as GitHub spells it in `user.login`. Stable across App
# renames only in its numeric id, so `check` compares logins case-insensitively.
BOT_LOGIN = "morgana-coding-agent[bot]"

MINT_SCRIPT = os.path.join(
    ct.REPO_ROOT, ".claude", "skills", "bcp-revise", "mint-bot-token.sh")

_GH_DEFAULT_WINDOWS = r"C:\Program Files\GitHub CLI\gh.exe"
_BASH_CANDIDATES = (
    r"C:\Program Files\Git\bin\bash.exe",
    r"C:\Program Files (x86)\Git\bin\bash.exe",
)


class GhError(RuntimeError):
    """A `gh` invocation failed; the message is its stderr."""


def find_gh():
    gh = shutil.which("gh")
    if gh:
        return gh
    if os.name == "nt" and os.path.exists(_GH_DEFAULT_WINDOWS):
        return _GH_DEFAULT_WINDOWS
    sys.exit("error: gh CLI not found on PATH (install: https://cli.github.com)")


def _find_bash():
    bash = shutil.which("bash")
    if bash:
        return bash
    for path in _BASH_CANDIDATES:
        if os.path.exists(path):
            return path
    return None


def bot_token():
    """A one-hour installation token for the bot, or None when it is not set up.

    Never raises: a machine with no key is a supported state, and the caller
    decides whether to fall back to the developer's own login or refuse.
    """
    bash = _find_bash()
    if not bash or not os.path.isfile(MINT_SCRIPT):
        return None
    result = subprocess.run(
        [bash, MINT_SCRIPT], capture_output=True, text=True, cwd=ct.REPO_ROOT)
    token = result.stdout.strip()
    if result.returncode != 0 or not token:
        return None
    return token


def run_gh(args, token=None, check=True):
    """Runs `gh` and returns its stdout. `token` is exported as GH_TOKEN."""
    env = dict(os.environ)
    if token:
        env["GH_TOKEN"] = token
        env["GITHUB_TOKEN"] = token
    else:
        # An inherited GH_TOKEN would silently outrank the logged-in account.
        env.pop("GH_TOKEN", None)
        env.pop("GITHUB_TOKEN", None)
    result = subprocess.run(
        [find_gh()] + args, capture_output=True, text=True, env=env, cwd=ct.REPO_ROOT)
    if check and result.returncode != 0:
        raise GhError(result.stderr.strip() or f"gh exited {result.returncode}")
    return result.stdout


def gh_json(args, token=None):
    out = run_gh(args, token=token)
    return json.loads(out) if out.strip() else None


def api(endpoint, token=None, method=None, fields=None, raw_fields=None):
    """One REST call. `endpoint` never starts with '/' -- MSYS rewrites that to a path."""
    args = ["api", endpoint.lstrip("/")]
    if method:
        args += ["-X", method]
    for key, value in (fields or {}).items():
        args += ["-f", f"{key}={value}"]
    for key, value in (raw_fields or {}).items():
        args += ["-F", f"{key}={value}"]
    return json.loads(run_gh(args, token=token) or "null")


def repo_slug(override=None):
    """owner/name for the REST endpoints."""
    if override:
        return override
    return gh_json(["repo", "view", "--json", "nameWithOwner"])["nameWithOwner"]


def login(token=None):
    """The login `token` acts as. An installation token has no user, hence the bot name."""
    if token:
        return BOT_LOGIN
    try:
        return run_gh(["api", "user", "--jq", ".login"]).strip()
    except GhError:
        return ""
