#!/usr/bin/env python3
"""Generate scripts/config.json, the machine-local settings the other scripts read.

Detects the toolchain (cmake, ninja, clang, clang-format, vcvarsall) the same way
the scripts do today, asks which CMake preset you want to work in, and writes the
answers down so neither has to happen again on every invocation. Anything that
can't be detected is prompted for; a blank answer leaves the key out, which just
means that tool keeps being looked up on PATH at run time.

config.json is git-ignored. It describes a machine, not the project -- see
scripts/config.example.json for the shape and scripts/util/config.py for the
schema.

Also offers to install `just` (the task runner behind the root justfile) and the
GitHub CLI `gh` (which bcp-revise uses for PR reviews). Both are optional -- every
recipe is a one-line call into these scripts, so `python scripts/build.py ...`
works without `just`, and only the review workflow needs `gh` -- which is why this
asks rather than installs.

Finally, offers to set up this developer's morgana-coding-agent key, which bcp-revise
posts PR review replies with. Also optional, and skipped by a blank answer. See
docs/ai-coding.md.

Bootstrap this with `python scripts/init.py`; afterwards it is `just init`.

Usage:
    just init                                       # detect, prompt, write
    just init --preset windows-clang-dx12-debug     # skip the preset prompt
    just init --show                                # print what would be written
    just init --force                               # overwrite without confirming
    just init --no-just                             # skip the `just` check
    just init --no-gh                               # skip the GitHub CLI check
    just init --no-bot                              # skip the morgana-coding-agent key setup
"""

import argparse
import os
import shutil
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg

REQUIREMENTS = os.path.join(ct.REPO_ROOT, "scripts", "requirements.txt")

# The shared morgana-coding-agent GitHub App that bcp-revise posts PR replies as. The
# App ID is not a secret -- it identifies the App, the same one for everyone -- so it
# lives here. Each developer supplies their own private key; see docs/ai-coding.md.
BOT_APP_ID = "4304152"
BOT_KEYS_URL = "https://github.com/settings/apps/morgana-coding-agent/keys"
BOT_DIR = os.path.join(os.path.expanduser("~"), ".claude")
BOT_KEY = os.path.join(BOT_DIR, "morgana-coding-agent.private-key.pem")
BOT_ENV = os.path.join(BOT_DIR, "morgana-coding-agent.env")


def selectable_presets():
    """The presets a user can actually choose: name + display name, hidden ones dropped."""
    data, _by_name = ct.load_presets()
    return [
        (p["name"], p.get("displayName", ""))
        for p in data.get("configurePresets", [])
        if not p.get("hidden")
    ]


def interactive():
    return sys.stdin.isatty()


def ask(question):
    """input() that reads a closed or redirected stdin as an empty answer.

    isatty() isn't dependable here -- Git Bash reports a tty even when stdin is
    redirected -- so EOF is what actually tells us nobody is there to answer, and
    every caller treats an empty answer as "take the default".
    """
    try:
        return input(question).strip()
    except EOFError:
        print()
        return ""


def ask_preset(default):
    presets = selectable_presets()
    if not presets:
        return default

    names = [name for name, _ in presets]
    if default not in names:
        default = names[0]

    if not interactive():
        return default

    print("preset:")
    width = max(len(name) for name in names)
    for i, (name, display) in enumerate(presets, start=1):
        mark = "  (default)" if name == default else ""
        print(f"  {i:>2}) {name:<{width}}  {display}{mark}")

    choice_default = names.index(default) + 1
    while True:
        raw = ask(f"select [{choice_default}]: ")
        if not raw:
            return default
        if raw in names:
            return raw
        if raw.isdigit() and 1 <= int(raw) <= len(names):
            return names[int(raw) - 1]
        print(f"  enter 1-{len(names)} or a preset name.", file=sys.stderr)


def ask_path(label, hint):
    """Prompt for a tool we couldn't find. An empty answer skips it."""
    if not interactive():
        print(f"missing   {label:<13} not found. {hint}", file=sys.stderr)
        return None
    print(f"\n{label} was not found. {hint}")
    while True:
        raw = ask(f"path to {label} (blank to skip): ").strip('"')
        if not raw:
            return None
        path = os.path.expanduser(os.path.expandvars(raw))
        if os.path.isfile(path) or os.path.isdir(path):
            return path
        print(f"  '{raw}' does not exist.", file=sys.stderr)


def confirm(question):
    return ask(f"{question} [y/N]: ").lower() in ("y", "yes")


def ensure_just():
    """Report on `just`, and offer to install it if it's missing.

    It comes from PyPI as `rust-just`, which ships the real binary as a per-platform
    wheel -- so this is one command on Windows, Linux and macOS, needs no Rust
    toolchain, and pins the same version for everyone (`scripts/requirements.txt`).
    """
    found = shutil.which("just")
    if found:
        print(f"just is installed: {found}")
        return

    print("\njust was not found. It runs the root justfile (`just build`, `just format`, ...).\n"
          "It is optional: `python scripts/build.py ...` works without it.")
    if not confirm(f"install it now (pip install -r {cfg.rel(REQUIREMENTS)})?"):
        print(f"skipped. Install it later with: pip install -r {cfg.rel(REQUIREMENTS)}")
        return

    cmd = [sys.executable, "-m", "pip", "install", "-r", REQUIREMENTS]
    if subprocess.run(cmd).returncode:
        print(f"warning: installing just failed. Install it by hand with: "
              f"pip install -r {cfg.rel(REQUIREMENTS)}", file=sys.stderr)
        return

    # pip drops it in a Scripts/bin dir that may not be on this process's PATH.
    installed = shutil.which("just")
    print(f"\ninstalled just{'  ' + installed if installed else ''}")
    if not installed:
        print("note: `just` isn't on PATH in this shell yet -- open a new one.")


def ensure_hooks():
    """Point git at the committed .githooks directory.

    core.hooksPath is machine-local config, not committed, so a fresh clone has to
    opt in once. The prepare-commit-msg hook there attributes commits to the
    morgana-coding-agent bot; see docs/ai-coding.md.
    """
    hooks_dir = ".githooks"
    try:
        current = subprocess.run(
            ["git", "config", "--get", "core.hooksPath"],
            cwd=ct.REPO_ROOT, capture_output=True, text=True,
        ).stdout.strip()
    except OSError:
        print("warning: git not found; skipped core.hooksPath setup.", file=sys.stderr)
        return

    if current == hooks_dir:
        print(f"git hooks: {hooks_dir}")
        return
    if subprocess.run(["git", "config", "core.hooksPath", hooks_dir], cwd=ct.REPO_ROOT).returncode:
        print("warning: could not set core.hooksPath.", file=sys.stderr)
        return
    print(f"git hooks: set core.hooksPath to {hooks_dir}")


def find_gh():
    """Path to the GitHub CLI, or None. Checks PATH, then the standard install dir.

    winget installs gh under Program Files, which isn't necessarily on this
    process's PATH even when it is on the user's -- so a PATH miss isn't absence.
    """
    found = shutil.which("gh")
    if found:
        return found
    if sys.platform == "win32":
        for var in ("ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"):
            base = os.environ.get(var)
            if base:
                candidate = os.path.join(base, "GitHub CLI", "gh.exe")
                if os.path.isfile(candidate):
                    return candidate
    return None


def ensure_gh():
    """Report on the GitHub CLI, which bcp-revise uses to read PR reviews and post
    replies. It is not a Python package -- the `gh` on PyPI is an unrelated project --
    so it is a manual install; only the review workflow needs it.
    """
    found = find_gh()
    if found:
        print(f"gh is installed: {found}")
        if not shutil.which("gh"):
            print(f"note: gh isn't on this shell's PATH; add {os.path.dirname(found)} to PATH.")
        return
    print("\ngh (GitHub CLI) was not found. bcp-revise uses it for PR reviews.\n"
          "Install it from https://cli.github.com/ and add it to PATH; it is not a pip package.")


def ensure_bot_key():
    """Set up this developer's key for the morgana-coding-agent App bcp-revise posts with.

    Optional: only devs who run the review-reply skill need it. Each dev uses their
    own private key for the shared App, so a leaked key is revoked per person without
    disturbing anyone else. The key is copied into ~/.claude (outside the repo, never
    committed); the App ID is written beside it. See docs/ai-coding.md.
    """
    if os.path.isfile(BOT_KEY) and os.path.isfile(BOT_ENV):
        print(f"bot key is configured: {BOT_KEY}")
        return
    if not interactive():
        return

    print("\nThe morgana-coding-agent GitHub App lets bcp-revise post PR review replies under a\n"
          "bot identity instead of your own account. It is optional -- set it up only if you\n"
          "run that skill. Generate your own private key (Generate a private key) at:\n"
          f"    {BOT_KEYS_URL}\n"
          "then give the path to the downloaded .pem. See docs/ai-coding.md.")
    raw = ask("path to the bot private key .pem (blank to skip): ").strip('"')
    if not raw:
        print("skipped; bcp-revise posts as your own account until you run this again.")
        return

    src = os.path.expanduser(os.path.expandvars(raw))
    if not os.path.isfile(src):
        print(f"  '{raw}' does not exist; skipped.", file=sys.stderr)
        return

    os.makedirs(BOT_DIR, exist_ok=True)
    shutil.copyfile(src, BOT_KEY)
    with open(BOT_ENV, "w", encoding="utf-8") as fh:
        fh.write(f"MORGANA_APP_ID={BOT_APP_ID}\n")
        fh.write("MORGANA_KEY=~/.claude/morgana-coding-agent.private-key.pem\n")
    for path in (BOT_KEY, BOT_ENV):
        try:
            os.chmod(path, 0o600)
        except OSError:
            pass  # NTFS via Git Bash ignores mode bits; the files are in the user profile.
    print(f"wrote {BOT_KEY}\nwrote {BOT_ENV}")


def detect(preset, arch):
    """Work out the config for `preset`. Returns (config dict, [(label, value), ...])."""
    generator = ct.generator_of(preset)
    if generator is None:
        print(f"warning: could not resolve a generator for preset '{preset}'; it may not exist "
              f"in CMakePresets.json.", file=sys.stderr)

    data = {"preset": preset, "arch": arch}
    notes = []

    # Only multi-config generators pick Debug/Release at build time; for the
    # others the preset already baked it into the cache, and passing --config
    # would be noise. The preset names carry the configuration, so read it off.
    if ct.is_multi_config(generator):
        lowered = preset.lower()
        if lowered.endswith("release"):
            data["config"] = "Release"
        elif lowered.endswith("debug"):
            data["config"] = "Debug"
        if "config" in data:
            notes.append(("config", data["config"]))

    # `precommand` exists for vcvarsall, the one thing that can't be resolved to a
    # path we invoke directly: it has to run first and mutate the environment.
    if ct.needs_msvc_env(generator):
        vcvars = ct.vcvarsall()
        if not vcvars:
            vcvars = ask_path(
                "vcvarsall.bat",
                "It puts MSVC and the Windows SDK on PATH, which this generator needs.",
            )
        if vcvars:
            data["precommand"] = f'"{vcvars}" {arch}'
            notes.append(("precommand", data["precommand"]))

    tools = {}

    cmake = ct.find_cmake()
    if not cmake:
        cmake = ask_path("cmake", "Install CMake, or point at the copy inside Visual Studio.")
    if cmake:
        tools["cmake"] = cmake

    if generator and "ninja" in generator.lower():
        ninja = ct.find_ninja()
        if not ninja:
            ninja = ask_path("ninja", "This preset's generator is Ninja, so a ninja binary is required.")
        if ninja:
            tools["ninja"] = ninja

    if ct.uses_clang(preset):
        clang = ct.find_clang()
        if clang:
            tools["clang"] = clang["c"]
        else:
            answer = ask_path(
                "clang",
                'Install the "C++ Clang tools for Windows" (LLVM) component from the Visual Studio '
                "Installer, or give the LLVM bin directory.",
            )
            if answer:
                tools["clang"] = answer

    # clang-format is needed by `just format` regardless of which compiler builds.
    clang_format = ct.find_clang_format()
    if not clang_format:
        clang_format = ask_path(
            "clang-format",
            'It ships with the "C++ Clang tools for Windows" (LLVM) component, or with LLVM itself.',
        )
    if clang_format:
        tools["clang-format"] = clang_format

    if tools:
        data["tools"] = tools
        notes.extend(tools.items())

    return data, notes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", help="CMake preset to record (default: ask).")
    parser.add_argument("--arch", help=f"vcvars architecture (default: {cfg.DEFAULT_ARCH}).")
    parser.add_argument("--force", action="store_true", help="Overwrite an existing config.json without asking.")
    parser.add_argument("--show", action="store_true", help="Print the config that would be written; write nothing.")
    parser.add_argument("--no-just", action="store_true", help="Don't check for (or offer to install) just.")
    parser.add_argument("--no-gh", action="store_true", help="Don't check for the GitHub CLI.")
    parser.add_argument("--no-bot", action="store_true", help="Don't offer to set up the morgana-coding-agent review key.")
    args = parser.parse_args()

    existing = cfg.load()
    if cfg.exists() and not args.force and not args.show:
        current = existing.get("preset", "?")
        if not confirm(f"{cfg.rel(cfg.PATH)} already exists (preset: {current}). Overwrite?"):
            print("aborted; nothing written.")
            return 1

    # An existing config's preset is the natural default when re-running init.
    preset = args.preset or ask_preset(existing.get("preset") or cfg.DEFAULT_PRESET)
    arch = args.arch or existing.get("arch") or cfg.DEFAULT_ARCH

    data, notes = detect(preset, arch)

    rows = [("preset", preset)] + notes
    width = max(len(label) for label, _ in rows)
    print()
    for label, value in rows:
        print(f"{label:<{width}}  {value}")

    if args.show:
        return 0

    cfg.save(data)
    print(f"\nwrote {cfg.rel(cfg.PATH)}")

    # After the config, so a failed/declined install never costs you the config.
    if not args.no_just:
        ensure_just()
    if not args.no_gh:
        ensure_gh()
    ensure_hooks()
    if not args.no_bot:
        ensure_bot_key()
    return 0


if __name__ == "__main__":
    sys.exit(main())
