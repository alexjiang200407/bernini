"""Locating -- and for `just init`, installing -- the vcpkg the presets build against.

Every configure preset inherits the hidden `vcpkg` preset, whose toolchain file is
`$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`. So a build needs both a vcpkg
checkout and an environment pointing at it; config.py exports VCPKG_ROOT into the
environment it hands cmake, which is what spares a developer from setting it by hand.

Which vcpkg it is does not matter -- vcpkg.json pins `builtin-baseline`, so the package
versions come from the manifest rather than from the checkout. It must, however, *contain*
that commit, which is why the clone here is not shallow.
"""

import os
import subprocess
import sys

import util.cmake_tools as ct

REPO_URL = "https://github.com/microsoft/vcpkg"

# What the presets' toolchainFile resolves to, relative to a vcpkg root.
TOOLCHAIN = os.path.join("scripts", "buildsystems", "vcpkg.cmake")


def is_root(path):
    """True when `path` looks like a vcpkg checkout, i.e. it holds the CMake toolchain."""
    return bool(path) and os.path.isfile(os.path.join(path, TOOLCHAIN))


def default_install_dir():
    return os.path.join(os.path.expanduser("~"), "vcpkg")


def candidates():
    """Places a vcpkg may already live, most authoritative first.

    VCPKG_INSTALLATION_ROOT is what the GitHub Actions images export; the rest are the
    conventional install locations and the package managers' own.
    """
    paths = [os.environ.get("VCPKG_ROOT"), os.environ.get("VCPKG_INSTALLATION_ROOT")]
    paths.append(default_install_dir())
    paths.append(os.path.join(ct.REPO_ROOT, "external", "vcpkg"))
    if sys.platform == "win32":
        paths += [r"C:\vcpkg", r"C:\dev\vcpkg", r"C:\src\vcpkg"]
    else:
        paths += ["/opt/vcpkg", "/usr/local/share/vcpkg", "/opt/homebrew/share/vcpkg"]
    return [os.path.expanduser(p) for p in paths if p]


def find():
    """The first vcpkg checkout that exists on this machine, or None."""
    for path in candidates():
        if is_root(path):
            return os.path.normpath(path)
    return None


def bootstrap_script(root):
    name = "bootstrap-vcpkg.bat" if sys.platform == "win32" else "bootstrap-vcpkg.sh"
    return os.path.join(root, name)


def executable(root):
    name = "vcpkg.exe" if sys.platform == "win32" else "vcpkg"
    path = os.path.join(root, name)
    return path if os.path.isfile(path) else None


def clone(dest):
    """Clone vcpkg into `dest`. Returns an error message, or None on success.

    Deliberately not `--depth 1`: vcpkg has to resolve the `builtin-baseline` commit
    vcpkg.json pins, which a shallow clone does not contain.
    """
    parent = os.path.dirname(os.path.abspath(dest))
    try:
        os.makedirs(parent, exist_ok=True)
    except OSError as exc:
        return f"could not create {parent}: {exc}"

    if os.path.exists(dest) and os.listdir(dest):
        return f"{dest} already exists and is not empty."

    try:
        rc = subprocess.run(["git", "clone", REPO_URL, dest]).returncode
    except OSError as exc:
        return f"could not run git: {exc}"
    return None if rc == 0 else f"`git clone {REPO_URL}` failed (exit {rc})."


def bootstrap(root):
    """Build the vcpkg executable in `root`. Returns an error message, or None on success."""
    script = bootstrap_script(root)
    if not os.path.isfile(script):
        return f"{script} does not exist."
    # CreateProcess cannot start a .bat itself, so the command processor runs it.
    cmd = ["cmd", "/c", script] if sys.platform == "win32" else [script]
    try:
        rc = subprocess.run(cmd + ["-disableMetrics"], cwd=root).returncode
    except OSError as exc:
        return f"could not run {os.path.basename(script)}: {exc}"
    return None if rc == 0 else f"{os.path.basename(script)} failed (exit {rc})."
