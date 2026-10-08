#!/usr/bin/env python3
"""Build the editor in Release and package it to share: `just package-editor`. Windows only.

Writes dist/bernini-editor-windows-x64/ and dist/bernini-editor-windows-x64.zip, replacing what an
earlier run left there. It is for beta testers: unsigned, and versioned only by the commit it was
built from. CI publishes it from every master push (.github/workflows/editor-release.yml).

What ships is the build's runtime directory with the developer's leftovers taken out: the editor,
its shaders, assets, translation catalogs and plugins, and every DLL beside it, which vcpkg's
applocal step and windeployqt already put there during the build. The MSVC runtime is added, so
the redistributable is not a prerequisite. The config is apps/editor/config.example.json rather
than whatever the build staged, which is a developer's own apps/editor/config.json when they have
one, with the graphics debug layers off: the D3D12 debug layer is an optional Windows feature a
tester rarely has, and without it the device does not open.

Not macOS: an app signed ad hoc is refused by Gatekeeper on a tester's machine until it is
notarized, which needs a Developer ID.

Usage:
    just package-editor                 # build windows-ninja-msvc-dx12-release, then package it
    just package-editor --no-build      # package what is built
    just package-editor -D NAME=VALUE   # passed to the build's configure (scripts/build.py -D)
    just package-editor --out D:/share  # somewhere other than ./dist
"""

import argparse
import datetime
import json
from pathlib import Path
import shutil
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg

ROOT = Path(ct.REPO_ROOT)
DIST = ROOT / "dist"
NAME = "bernini-editor-windows-x64"
PRESET = "windows-ninja-msvc-dx12-release"
VERSION = "0.1.0"

# What a runtime directory holds that is the developer's, not the editor's.
LEFTOVERS = {"config.json", ".assets.stamp"}
LEFTOVER_SUFFIXES = (".log", ".pdb", ".ilk", ".exp", ".lib", ".csv")
LEFTOVER_DIRS = {"shadercache"}
# Where a file is data whatever its extension.
DATA_DIRS = {"assets", "localization", "plugins"}


def ships(path, root):
    """Whether a file under the runtime directory is part of the editor rather than left there."""
    rel = path.relative_to(root)
    if rel.parts[0] in LEFTOVER_DIRS or path.name in LEFTOVERS:
        return False
    if path.suffix.lower() in LEFTOVER_SUFFIXES and rel.parts[0] not in DATA_DIRS:
        return False
    # The editor is the one program here; anything else is a build tool (bgpu_idlgen).
    if path.suffix.lower() == ".exe" and path.name.lower() != "editor.exe":
        return False
    return True


def write_config(into):
    config = json.loads((ROOT / "apps" / "editor" / "config.example.json").read_text())
    config["graphics"]["enableDebugLayer"] = False
    config["graphics"]["enablePixDebug"] = False
    config["memoryReport"] = False
    (into / "config.json").write_text(json.dumps(config, indent=4) + "\n")


def commit():
    try:
        sha = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"], cwd=ROOT, check=True,
                             capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT,
                               check=True, capture_output=True, text=True).stdout.strip()
        return f"{sha}{'+local changes' if dirty else ''}"
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def msvc_runtime(env):
    """The MSVC runtime DLLs, from the redistributable folder of the toolset that built the editor."""
    # Case-insensitive: a captured vcvars environment keeps cmd's casing, os.environ's copy does not.
    redist = next((value for key, value in env.items() if key.upper() == "VCTOOLSREDISTDIR"), None)
    if not redist:
        sys.exit("VCToolsRedistDir is unset: no vcvars environment was found to build in")
    crt = sorted(Path(redist, "x64").glob("Microsoft.VC*.CRT"))
    if not crt:
        sys.exit(f"{redist}: no x64 Microsoft.VC*.CRT folder")
    return sorted(crt[-1].glob("*.dll"))


def package(bin_dir, folder, env):
    for path in sorted(bin_dir.rglob("*")):
        if path.is_file() and ships(path, bin_dir):
            target = folder / path.relative_to(bin_dir)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    for dll in msvc_runtime(env):
        shutil.copy2(dll, folder / dll.name)
    write_config(folder)
    stamp = f"{VERSION}, engine {commit()}, packaged {datetime.date.today():%Y-%m-%d}"
    readme = (ROOT / "scripts" / "package_editor" / "README.txt").read_text()
    (folder / "README.txt").write_text(readme.format(version=stamp))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(DIST), help=f"Output directory (default: {DIST}).")
    parser.add_argument("--no-build", action="store_true", help="Package what is built; don't build first.")
    parser.add_argument("-D", "--define", action="append", default=[], metavar="NAME=VALUE",
                        help="A cache variable for the build's configure, as scripts/build.py -D takes it.")
    args = parser.parse_args()

    if sys.platform != "win32":
        sys.exit("the editor packages on Windows only")

    if not args.no_build:
        rc = subprocess.run([sys.executable, str(ROOT / "scripts" / "build.py"), "editor",
                             "--preset", PRESET, "--no-api",
                             *(f"-D{define}" for define in args.define)]).returncode
        if rc:
            return rc

    bin_dir = Path(ct.binary_dir_of(PRESET)) / "bin"
    if not (bin_dir / "editor.exe").exists():
        sys.exit(f"{bin_dir}: no editor.exe; the build found no Qt (put it on CMAKE_PREFIX_PATH)")

    out = Path(args.out)
    folder, zip_path = out / NAME, out / f"{NAME}.zip"
    for old in (folder, zip_path):  # this script's own output, and nothing else
        if old.is_dir():
            shutil.rmtree(old)
        elif old.exists():
            old.unlink()
    folder.mkdir(parents=True)

    env, _ = cfg.build_env(ct.generator_of(PRESET))
    package(bin_dir, folder, env)
    shutil.make_archive(str(out / NAME), "zip", out, NAME)

    size = sum(p.stat().st_size for p in folder.rglob("*") if p.is_file()) / 1e6
    print(f"{folder} ({size:.0f} MB)\n{zip_path} ({zip_path.stat().st_size / 1e6:.0f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
