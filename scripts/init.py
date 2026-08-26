#!/usr/bin/env python3
"""Set this machine up to build Bernini, and write down what it found.

One command is meant to take a fresh clone to a working build, so this both *detects* and
*installs*. Anything missing that can be obtained without a decision is offered:

  * vcpkg -- located, or cloned and bootstrapped. Its path is recorded and exported as
    VCPKG_ROOT by every build, so the environment variable never has to be set by hand.
  * cmake, ninja, clang-format, clang-tidy -- detected first, else installed from the
    version pinned in scripts/requirements.txt, which ships each as a real binary wheel.
    A machine that already has one keeps the one it has.
  * `just`, the task runner behind the root justfile, the same way.
  * Git LFS and the GitHub CLI, through this platform's package manager, since neither
    has a wheel.

Then it asks which CMake preset you work in and writes config.json, so no preset, no
tool path and no vcvars invocation has to be retyped. Anything that could not be found or
installed is prompted for; a blank answer leaves the key out, which just means that tool
keeps being looked up on PATH at run time.

config.json is git-ignored. It describes a machine, not the project; scripts/util/config.py
documents its schema.

Points git at the committed .githooks, and configures Git LFS -- whose filters are
machine-local config a clone cannot inherit, so without this the assets check out as
pointer text and the tests fail on them as though they were corrupt. Fetching them needs no
credentials -- the store serves reads publicly -- so the key it asks about is only for
adding assets, and skipping it leaves a working clone. See docs/lfs.md.

Finally, offers to set up this developer's morgana-coding-agent key, which bcp-revise
posts PR review replies with. Optional, and skipped by a blank answer. See
docs/ai-coding.md.

Bootstrap this with `python scripts/init.py`; afterwards it is `just init`.

Usage:
    just init                                       # detect, install, prompt, write
    just init --preset windows-clang-dx12-debug     # skip the preset prompt
    just init --show                                # print what would be written
    just init --force                               # overwrite without confirming
    just init --no-just                             # skip the `just` check
    just init --no-gh                               # skip the GitHub CLI check
    just init --no-lfs                              # skip the Git LFS setup
    just init --lfs-key                             # replace the stored LFS credentials
    just init --no-vcpkg                            # skip the vcpkg check
    just init --no-bot                              # skip the morgana-coding-agent key setup
"""

import argparse
import getpass
import os
import shutil
import subprocess
import sys
import sysconfig

import util.cmake_tools as ct
import util.config as cfg
import util.lfs as lfs
import util.lfs_store as lfs_store
import util.secrets as secrets
import util.vcpkg as vcpkg

REQUIREMENTS = os.path.join(ct.REPO_ROOT, "scripts", "requirements.txt")

# The shared morgana-coding-agent GitHub App that bcp-revise posts PR replies as. The
# App ID is not a secret -- it identifies the App, the same one for everyone -- so it
# lives here. Each developer supplies their own private key; see docs/ai-coding.md.
BOT_APP_ID = "4304152"
BOT_KEYS_URL = "https://github.com/settings/apps/morgana-coding-agent/keys"
BOT_DIR = os.path.join(os.path.expanduser("~"), ".claude")
BOT_KEY = os.path.join(BOT_DIR, "morgana-coding-agent.private-key.pem")
BOT_ENV = os.path.join(BOT_DIR, "morgana-coding-agent.env")

# Tools that come from a pinned wheel in requirements.txt when the machine has none. The
# wheel is named after the executable it installs in every case.
WHEEL_TOOLS = ("cmake", "ninja", "clang-format", "clang-tidy")

# Package-manager ids for the tools with no wheel, by manager.
PACKAGE_IDS = {
    "gh": {"winget": "GitHub.cli", "brew": "gh", "apt": "gh"},
    "git-lfs": {"winget": "GitHub.GitLFS", "brew": "git-lfs", "apt": "git-lfs"},
}


if sys.platform == "darwin":
    CLANG_HINT = "Install the Xcode Command Line Tools (xcode-select --install), or `brew install llvm`."
    CLANG_FORMAT_HINT = "It ships with LLVM: `brew install clang-format`, or `brew install llvm`."
    CMAKE_HINT = "Install CMake: `brew install cmake`."
    NINJA_HINT = "This preset's generator is Ninja: `brew install ninja`."
    GIT_LFS_HINT = "Install it with `brew install git-lfs`."
else:
    CLANG_HINT = (
        'Install the "C++ Clang tools for Windows" (LLVM) component from the Visual Studio '
        "Installer, or give the LLVM bin directory."
    )
    CLANG_FORMAT_HINT = (
        'It ships with the "C++ Clang tools for Windows" (LLVM) component, or with LLVM itself.'
    )
    CMAKE_HINT = "Install CMake, or point at the copy inside Visual Studio."
    NINJA_HINT = "This preset's generator is Ninja, so a ninja binary is required."
    GIT_LFS_HINT = "It ships with Git for Windows; otherwise install it from https://git-lfs.com."


def selectable_presets():
    """The presets a user can actually choose: name + display name, hidden ones dropped.

    Presets whose `condition` rules out this host are dropped too, so the menu never
    offers one that cannot configure here.
    """
    data, _by_name = ct.load_presets()
    return [
        (p["name"], p.get("displayName", ""))
        for p in data.get("configurePresets", [])
        if not p.get("hidden") and ct.runs_on_host(p["name"])
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


# --- Installing ------------------------------------------------------------

def pinned(package):
    """The requirement line for `package` from requirements.txt, e.g. "cmake==3.31.6".

    requirements.txt is the one place a version is written down, but init installs one
    tool at a time rather than the whole file, so the pin is read back out of it.
    """
    try:
        with open(REQUIREMENTS, encoding="utf-8") as fh:
            lines = fh.readlines()
    except OSError:
        return package

    for line in lines:
        line = line.split("#")[0].strip()
        name = line.split("==")[0].split(">=")[0].strip()
        if line and name.lower() == package.lower():
            return line
    return package


def pip_install(requirement):
    """Install one pinned requirement. Returns True on success."""
    cmd = [sys.executable, "-m", "pip", "install", requirement]
    print("  " + " ".join(cmd))
    try:
        return subprocess.run(cmd).returncode == 0
    except OSError as exc:
        print(f"warning: could not run pip: {exc}", file=sys.stderr)
        return False


def script_dirs():
    """Directories pip drops console scripts into, whether or not they are on PATH.

    A wheel installed into the user site (or into a Python whose Scripts/bin directory
    the current shell never had) is invisible to shutil.which, so a freshly installed
    tool would look as missing as it did before.
    """
    dirs = []
    for scheme in (None, f"{os.name}_user"):
        try:
            path = sysconfig.get_path("scripts") if scheme is None \
                else sysconfig.get_path("scripts", scheme)
        except KeyError:
            continue
        if path and path not in dirs:
            dirs.append(path)
    return dirs


def find_installed(name):
    """Locate `name` on PATH or in pip's script directories."""
    found = shutil.which(name)
    if found:
        return found
    exe = name + (".exe" if sys.platform == "win32" else "")
    for directory in script_dirs():
        candidate = os.path.join(directory, exe)
        if os.path.isfile(candidate):
            return candidate
    return None


def offer_wheel(label, purpose):
    """Offer to install `label` from its pinned wheel. Returns its path, or None.

    Declining is not a failure: everything installed here is also satisfiable by a copy
    that is already on the machine, which is what the caller falls back to asking for.
    """
    requirement = pinned(label)
    print(f"\n{label} was not found. {purpose}\n"
          f"It ships as a prebuilt wheel, so this needs no compiler or LLVM install.")
    if not confirm(f"install it now (pip install {requirement})?"):
        return None
    if not pip_install(requirement):
        print(f"warning: installing {label} failed. Install it by hand with: "
              f"pip install {requirement}", file=sys.stderr)
        return None

    found = find_installed(label)
    if not found:
        print(f"warning: {label} installed but could not be located afterwards.", file=sys.stderr)
        return None
    if not shutil.which(label):
        print(f"note: {label} isn't on this shell's PATH; the scripts use the recorded path.")
    return found


def package_manager():
    """(name, install command prefix) for this platform's package manager, or None."""
    if sys.platform == "win32" and shutil.which("winget"):
        return "winget", ["winget", "install", "-e", "--id"]
    if sys.platform == "darwin" and shutil.which("brew"):
        return "brew", ["brew", "install"]
    if shutil.which("apt-get"):
        return "apt", ["sudo", "apt-get", "install", "-y"]
    return None


def offer_package(label, purpose, fallback):
    """Offer to install a tool that has no wheel through the platform's package manager.

    Returns True when it is installed afterwards. `fallback` is what to tell someone this
    cannot help -- no package manager, an unknown platform, or a failed install.
    """
    manager = package_manager()
    ids = PACKAGE_IDS.get(label, {})
    package = ids.get(manager[0]) if manager else None

    if not package:
        print(f"\n{label} was not found. {purpose}\n{fallback}")
        return False

    name, prefix = manager
    print(f"\n{label} was not found. {purpose}")
    if not confirm(f"install it now ({name} install {package})?"):
        print(f"skipped. {fallback}")
        return False

    try:
        rc = subprocess.run(prefix + [package]).returncode
    except OSError as exc:
        print(f"warning: could not run {name}: {exc}", file=sys.stderr)
        rc = 1
    if rc:
        print(f"warning: installing {label} failed. {fallback}", file=sys.stderr)
        return False

    if not shutil.which(label):
        print(f"note: {label} isn't on this shell's PATH yet -- open a new one.")
    return True


def ensure_just():
    """Report on `just`, and offer to install it if it's missing.

    It comes from PyPI as `rust-just`, which ships the real binary as a per-platform
    wheel -- so this is one command on Windows, Linux and macOS, needs no Rust
    toolchain, and pins the same version for everyone (`scripts/requirements.txt`).
    """
    found = find_installed("just")
    if found:
        print(f"just is installed: {found}")
        return

    requirement = pinned("rust-just")
    print("\njust was not found. It runs the root justfile (`just build`, `just format`, ...).\n"
          "It is optional: `python scripts/build.py ...` works without it.")
    if not confirm(f"install it now (pip install {requirement})?"):
        print(f"skipped. Install it later with: pip install {requirement}")
        return
    if not pip_install(requirement):
        print(f"warning: installing just failed. Install it by hand with: "
              f"pip install {requirement}", file=sys.stderr)
        return

    installed = shutil.which("just")
    print(f"\ninstalled just{'  ' + installed if installed else ''}")
    if not installed:
        print("note: `just` isn't on PATH in this shell yet -- open a new one.")


def ensure_hooks_executable():
    """Restore the exec bit on the hooks, which git silently skips without it.

    The committed modes are 100755, so a clone gets this right; an archive export or
    a copy across a filesystem that drops mode bits does not, and the only symptom is
    a one-line "hook was ignored" hint. No-op on Windows, where chmod does nothing.
    """
    hooks_dir = os.path.join(ct.REPO_ROOT, ".githooks")
    if sys.platform == "win32" or not os.path.isdir(hooks_dir):
        return

    for name in sorted(os.listdir(hooks_dir)):
        path = os.path.join(hooks_dir, name)
        if not os.path.isfile(path) or os.access(path, os.X_OK):
            continue
        mode = os.stat(path).st_mode
        try:
            os.chmod(path, mode | ((mode & 0o444) >> 2))
        except OSError as exc:
            print(f"warning: could not make {name} executable: {exc}", file=sys.stderr)
        else:
            print(f"git hooks: made {name} executable")


def ensure_hooks():
    """Point git at the committed .githooks directory.

    core.hooksPath is machine-local config, not committed, so a fresh clone has to
    opt in once. The prepare-commit-msg hook there attributes commits to the
    morgana-coding-agent bot; see docs/ai-coding.md.
    """
    ensure_hooks_executable()

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


def ensure_lfs_agent(replace=False):
    """Point this clone's Git LFS at the project's own object store.

    The store's location is committed in .lfsstore, but the two settings that make
    git-lfs use it cannot be: git-lfs ignores `standalonetransferagent` and
    `customtransfer.*` from .lfsconfig, since a file that arrives with a clone must not
    get to name the program git executes. They are machine-local, which is this script's
    job -- and they have to name absolute paths, because git-lfs runs the agent from
    whatever directory the triggering git command was in.

    Returns False when the credentials are missing, so the caller does not then try a
    fetch that can only fail.
    """
    endpoint = lfs_store.setting("endpoint")
    if not endpoint:
        return True

    agent = os.path.join(ct.REPO_ROOT, "scripts", "lfs_agent.py")
    settings = {
        "lfs.standalonetransferagent": "bernini",
        "lfs.customtransfer.bernini.path": sys.executable,
        # git-lfs quotes `path` for itself but interpolates `args` into a shell command
        # verbatim, so an unquoted path splits on the first space in it.
        "lfs.customtransfer.bernini.args": f'"{agent}"',
        # .lfsconfig excludes everything so that a clone with no agent yet writes pointers
        # instead of failing its checkout. This is the line that takes it back off, and it
        # has to be local config, which outranks the committed file.
        "lfs.fetchexclude": "",
    }

    for key, value in settings.items():
        if subprocess.run(["git", "config", "--local", key, value],
                          cwd=ct.REPO_ROOT).returncode:
            print(f"warning: could not set {key}; run `git config --local {key} \"{value}\"` "
                  f"by hand.", file=sys.stderr)
            return False

    read_url = lfs_store.setting("readUrl")
    if read_url:
        print(f"git lfs: reads come from {read_url}, writes go to {endpoint}")
    else:
        print(f"git lfs: transfers go to {endpoint}")

    if endpoint.startswith("file://"):
        return True

    return ensure_lfs_credentials(replace)


def ensure_lfs_credentials(replace=False):
    """Make sure this machine can reach the LFS object store, storing the key if asked.

    Returns whether this clone can *fetch* assets, which is what decides whether the caller
    then pulls them. With a public read endpoint configured that is true with no key at
    all, and a key is only what lets this machine add one.

    The secret is encrypted to this user account before it is written, so config.json --
    which is git-ignored but is also the file people paste into bug reports -- does not
    become the credential itself.

    With `replace`, asks even when a usable key is already stored, which is how a leaked
    one is rotated.
    """
    try:
        access, secret = lfs_store.credentials()
    except lfs_store.StoreError as exc:
        print(f"warning: {exc}", file=sys.stderr)
        access, secret = "", ""

    if access and secret and not replace:
        stored = cfg.load().get("lfs") or {}
        where = "environment" if os.environ.get("BERNINI_LFS_ACCESS_KEY_ID") else \
            f"{cfg.rel(cfg.PATH)}, {secrets.describe(stored.get('secretAccessKey'))}"
        print(f"git lfs: credentials from the {where}")
        return True

    read_url = lfs_store.setting("readUrl")

    # Fetching is the only thing most clones ever do, and a public read endpoint covers it.
    # So a key is offered rather than demanded, and declining is a working setup, not a
    # half-finished one.
    if read_url and not replace:
        print(f"git lfs: assets download from {read_url}; this clone needs no key to fetch "
              f"them.")
        if not interactive() or not confirm("set up a key that can also *add* assets?"):
            print("         Skipped -- run `just init --lfs-key` if you need to add assets "
                  "later.")
            return True

    print("\nAdding an asset means uploading it, which needs an access key. Create one in\n"
          "Cloudflare R2 (Object Read & Write is enough) and paste it here, or leave blank\n"
          "to skip and set BERNINI_LFS_ACCESS_KEY_ID / BERNINI_LFS_SECRET_ACCESS_KEY\n"
          "yourself.")

    key_id = ask("  access key id: ")
    if not key_id:
        if read_url:
            print(f"note: nothing stored; assets still download from {read_url}, but adding "
                  f"one needs a key.")
            return True
        print("warning: no credentials stored; assets will stay as pointer text until\n"
              "         they are provided. See docs/lfs.md.", file=sys.stderr)
        return False

    key_secret = getpass.getpass("  secret access key (not echoed): ").strip()
    if not key_secret:
        print("warning: no secret given; nothing stored.", file=sys.stderr)
        return bool(read_url)

    try:
        protected = secrets.protect(key_secret)
    except secrets.SecretError as exc:
        print(f"warning: could not protect the secret ({exc}); nothing stored.", file=sys.stderr)
        return bool(read_url)

    data = cfg.load()
    data["lfs"] = {"accessKeyId": key_id, "secretAccessKey": protected}
    cfg.save(data)
    secrets.restrict(cfg.PATH)

    if secrets.scheme() == "plain":
        print(f"warning: this platform has no key store, so the secret is in "
              f"{cfg.rel(cfg.PATH)} as\n         plaintext. Treat that file as the "
              f"credential.", file=sys.stderr)
    else:
        print(f"git lfs: stored in {cfg.rel(cfg.PATH)}, {secrets.describe(protected)}")
    return True


def ensure_lfs(replace=False):
    """Configure Git LFS for this clone, and fetch anything still left as a pointer.

    The `filter.lfs.*` entries live in machine-local git config, which cannot be
    committed, so a fresh clone has no smudge filter and writes 130-byte pointer text
    into the working tree instead of the asset. The committed .githooks do call
    `git lfs post-checkout`, but git-lfs declines to check anything out until those
    filters exist -- so the hooks cannot cover for this, and `git lfs pull` reports
    success while doing nothing.

    Nothing surfaces that as a setup problem: the tests read assets/ and fail as a
    corrupt .glb ("Invalid magic") instead.

    Installed with --local, so a machine that keeps per-repo git identities keeps them.
    """
    if not shutil.which("git-lfs") and not offer_package(
        "git-lfs",
        "The assets under assets/ are stored with Git LFS, and without it they check out as "
        "text pointers\nand the tests fail on them as though they were corrupt.",
        GIT_LFS_HINT,
    ):
        return

    configured = subprocess.run(
        ["git", "config", "--get", "filter.lfs.process"],
        cwd=ct.REPO_ROOT, capture_output=True, text=True,
    ).stdout.strip()

    if configured:
        print("git lfs: filters configured")
    elif subprocess.run(["git", "lfs", "install", "--local"], cwd=ct.REPO_ROOT).returncode:
        print("warning: `git lfs install --local` failed; run it by hand.", file=sys.stderr)
        return
    else:
        print("git lfs: installed the filters for this clone")

    ready = ensure_lfs_agent(replace)

    stale = lfs.pointer_files()
    if not stale or not ready:
        return

    print(f"git lfs: {len(stale)} file(s) are still pointers; fetching...")
    if subprocess.run(["git", "lfs", "pull"], cwd=ct.REPO_ROOT).returncode:
        print("warning: `git lfs pull` failed; run it by hand.", file=sys.stderr)
        return

    remaining = lfs.pointer_files()
    if remaining:
        print(f"warning: {len(remaining)} file(s) are still pointers, e.g. {remaining[0]}. "
              f"Run `git lfs pull` by hand.", file=sys.stderr)
    else:
        print(f"git lfs: fetched {len(stale)} file(s)")


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
    """Report on the GitHub CLI, and offer to install it through the package manager.

    bcp-revise uses it to read PR reviews and post replies. It is not a Python package --
    the `gh` on PyPI is an unrelated project -- so it cannot come from a pinned wheel like
    the rest; winget and brew are the way, and only the review workflow needs it at all.
    """
    found = find_gh()
    if found:
        print(f"gh is installed: {found}")
        if not shutil.which("gh"):
            print(f"note: gh isn't on this shell's PATH; add {os.path.dirname(found)} to PATH.")
        return

    offer_package(
        "gh",
        "bcp-revise uses the GitHub CLI to read PR reviews and post replies.",
        "Install it from https://cli.github.com/ and add it to PATH; it is not a pip package.",
    )


def ensure_vcpkg(install=True):
    """Path to a vcpkg checkout to build against, cloning one when there is none.

    Every preset's toolchain file resolves through $env{VCPKG_ROOT}, and the recorded path
    is what the build scripts export it as -- so this is what replaces setting the variable
    on the machine by hand. Bootstrapped here rather than left to the CMake toolchain so a
    broken checkout is reported now, by name, instead of mid-configure.
    """
    found = cfg.find_vcpkg()
    if found:
        if install and not vcpkg.executable(found):
            print(f"vcpkg: bootstrapping {found}")
            error = vcpkg.bootstrap(found)
            if error:
                print(f"warning: {error} Run {vcpkg.bootstrap_script(found)} by hand.",
                      file=sys.stderr)
        return found

    if not install or not interactive():
        print(f"missing   {'vcpkg':<13} not found. Clone {vcpkg.REPO_URL} and re-run this, "
              f"or set VCPKG_ROOT.", file=sys.stderr)
        return None

    default = vcpkg.default_install_dir()
    print(f"\nvcpkg was not found. Every preset builds its dependencies with it.\n"
          f"It can be cloned and bootstrapped now (about 1 GB; shared by every project on this\n"
          f"machine, and the package versions come from vcpkg.json, not from the checkout).")
    if not confirm(f"clone {vcpkg.REPO_URL} into {default}?"):
        raw = ask("path to an existing vcpkg checkout (blank to skip): ").strip('"')
        if not raw:
            print("skipped; builds fail until a vcpkg is found or VCPKG_ROOT is set.")
            return None
        path = os.path.expanduser(os.path.expandvars(raw))
        if vcpkg.is_root(path):
            return os.path.normpath(path)
        print(f"  '{raw}' is not a vcpkg checkout (no {vcpkg.TOOLCHAIN}); skipped.", file=sys.stderr)
        return None

    error = vcpkg.clone(default)
    if error:
        print(f"warning: {error}", file=sys.stderr)
        return None
    error = vcpkg.bootstrap(default)
    if error:
        print(f"warning: cloned, but {error}", file=sys.stderr)
    return default


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


def obtain(label, finder, purpose, hint, install=True):
    """Path to `label`: detected, else installed from its pinned wheel, else asked for."""
    found = finder()
    if found:
        return found
    if install and label in WHEEL_TOOLS and interactive():
        found = offer_wheel(label, purpose)
    return found or ask_path(label, hint)


def detect(preset, arch, install=True, with_vcpkg=True):
    """Work out the config for `preset`. Returns (config dict, [(label, value), ...]).

    With `install` off nothing is downloaded -- what is already on the machine is recorded
    and the rest is left out, which is what --show reports.
    """
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

    # The presets' toolchain file resolves through VCPKG_ROOT, which the build scripts
    # export from here rather than expecting the machine to define it.
    if with_vcpkg:
        root = ensure_vcpkg(install)
        if root:
            data["vcpkg"] = root
            notes.append(("vcpkg", root))

    tools = {}

    cmake = obtain("cmake", ct.find_cmake, "It is the buildsystem this project is driven by.",
                   CMAKE_HINT, install)
    if cmake:
        tools["cmake"] = cmake

    if generator and "ninja" in generator.lower():
        ninja = obtain("ninja", ct.find_ninja, "This preset's generator is Ninja.", NINJA_HINT,
                       install)
        if ninja:
            tools["ninja"] = ninja

    # The compiler is the one tool with no wheel to fall back on: it has to match the
    # platform SDK it builds against.
    if ct.uses_clang(preset):
        clang = ct.find_clang()
        if clang:
            tools["clang"] = clang["c"]
        else:
            answer = ask_path("clang", CLANG_HINT)
            if answer:
                tools["clang"] = answer

    # clang-format is needed by `just format` regardless of which compiler builds.
    clang_format = obtain("clang-format", ct.find_clang_format,
                          "`just format` and the pre-commit hook run it on every source file "
                          "you touch.", CLANG_FORMAT_HINT, install)
    if clang_format:
        tools["clang-format"] = clang_format

    # clang-tidy is never asked for by path: `just tidy` degrades to a message that says how to
    # install it, and the pre-commit hook skips the naming check when it is absent.
    clang_tidy = ct.find_clang_tidy()
    if not clang_tidy and install and interactive():
        clang_tidy = offer_wheel("clang-tidy", "`just tidy` checks the naming rules with it, "
                                               "and the pre-commit hook checks the lines you "
                                               "changed.")
    if clang_tidy:
        tools["clang-tidy"] = clang_tidy

    # pytest runs `just test`'s scripts_tests suite. It is a library rather than a located
    # tool, so it is only offered here -- nothing about it reaches config.json.
    if install and interactive() and not find_installed("pytest"):
        offer_wheel("pytest", "`just test` runs the scripts/tests suite with it.")

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
    parser.add_argument("--no-lfs", action="store_true", help="Don't configure Git LFS or fetch its files.")
    parser.add_argument("--lfs-key", action="store_true",
                        help="Ask for the key that uploads assets, replacing what is stored.")
    parser.add_argument("--no-vcpkg", action="store_true", help="Don't look for (or offer to clone) vcpkg.")
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

    data, notes = detect(preset, arch, install=not args.show, with_vcpkg=not args.no_vcpkg)

    # detect() describes the toolchain and nothing else, so anything else already in the
    # file -- the LFS credential in particular -- has to be carried across a re-run.
    if existing.get("lfs"):
        data["lfs"] = existing["lfs"]

    rows = [("preset", preset)] + notes
    width = max(len(label) for label, _ in rows)
    print()
    for label, value in rows:
        print(f"{label:<{width}}  {value}")

    if args.show:
        return 0

    cfg.save(data)
    secrets.restrict(cfg.PATH)
    print(f"\nwrote {cfg.rel(cfg.PATH)}")

    # After the config, so a failed/declined install never costs you the config.
    if not args.no_just:
        ensure_just()
    if not args.no_gh:
        ensure_gh()
    ensure_hooks()
    if not args.no_lfs:
        ensure_lfs(args.lfs_key)
    if not args.no_bot:
        ensure_bot_key()
    return 0


if __name__ == "__main__":
    sys.exit(main())
