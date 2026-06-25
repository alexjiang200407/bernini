"""Shared helpers for the Bernini CMake scripts.

Everything here is generator-agnostic. Target/artifact discovery goes through the
CMake File API codemodel (identical JSON for Ninja, Xcode, Make and VS/MSBuild),
and the Visual Studio toolchain is located via vswhere rather than hardcoded paths.
"""

import glob
import json
import os
import shutil
import subprocess
import sys

# This module lives in scripts/util/, so the repo root is two levels up.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Debug/symbol/import sidecars that show up as artifacts but aren't runnable binaries.
NON_BINARY_SUFFIXES = (".pdb", ".ilk", ".exp", ".lib", ".map", ".dwo")


# --- CMake File API --------------------------------------------------------

def _api_dir(build_dir):
    return os.path.join(build_dir, ".cmake", "api", "v1")


def _reply_dir(build_dir):
    return os.path.join(_api_dir(build_dir), "reply")


def ensure_query(build_dir):
    """Drop a stateless query so the codemodel is (re)generated on next configure."""
    query_dir = os.path.join(_api_dir(build_dir), "query")
    try:
        os.makedirs(query_dir, exist_ok=True)
        stamp = os.path.join(query_dir, "codemodel-v2")
        if not os.path.exists(stamp):
            open(stamp, "w").close()
    except OSError:
        pass  # best-effort; reading still works if a reply already exists


def find_build_dirs(explicit=None):
    """Return build dirs that have a populated File API reply (or just `explicit`)."""
    if explicit:
        return [explicit]
    dirs = []
    pattern = os.path.join(REPO_ROOT, "build", "*", ".cmake", "api", "v1", "reply")
    for reply in glob.glob(pattern):
        dirs.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(reply)))))
    return sorted(dirs)


def _codemodel(build_dir):
    reply = _reply_dir(build_dir)
    indexes = sorted(glob.glob(os.path.join(reply, "index-*.json")))
    if not indexes:
        return None, reply
    with open(indexes[-1], encoding="utf-8") as fh:
        index = json.load(fh)
    for obj in index.get("objects", []):
        if obj.get("kind") == "codemodel":
            with open(os.path.join(reply, obj["jsonFile"]), encoding="utf-8") as fh:
                return json.load(fh), reply
    return None, reply


def load_targets(build_dir):
    """Every target across every configuration.

    Returns a list of {name, type, config, artifacts: [abs path, ...]}.
    """
    codemodel, reply = _codemodel(build_dir)
    if codemodel is None:
        return []

    targets = []
    for config in codemodel.get("configurations", []):
        config_name = config.get("name", "")
        for ref in config.get("targets", []):
            with open(os.path.join(reply, ref["jsonFile"]), encoding="utf-8") as fh:
                target = json.load(fh)
            artifacts = [
                os.path.normpath(os.path.join(build_dir, a["path"]))
                for a in target.get("artifacts", [])
                if not a["path"].lower().endswith(NON_BINARY_SUFFIXES)
            ]
            targets.append(
                {
                    "name": target["name"],
                    "type": target.get("type", ""),
                    "config": config_name,
                    "artifacts": artifacts,
                }
            )
    return targets


# --- CMake presets ---------------------------------------------------------

def load_presets():
    """Return (raw json, {name: configurePreset})."""
    path = os.path.join(REPO_ROOT, "CMakePresets.json")
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    by_name = {p["name"]: p for p in data.get("configurePresets", [])}
    return data, by_name


def _resolve_field(name, by_name, field, seen=None):
    """First value of `field` found in the preset or, depth-first, its inherits."""
    seen = seen or set()
    if name in seen or name not in by_name:
        return None
    seen.add(name)
    preset = by_name[name]
    if field in preset:
        return preset[field]
    inherits = preset.get("inherits", [])
    if isinstance(inherits, str):
        inherits = [inherits]
    for parent in inherits:
        value = _resolve_field(parent, by_name, field, seen)
        if value is not None:
            return value
    return None


def generator_of(preset):
    _, by_name = load_presets()
    return _resolve_field(preset, by_name, "generator")


def binary_dir_of(preset):
    _, by_name = load_presets()
    binary_dir = _resolve_field(preset, by_name, "binaryDir")
    if not binary_dir:
        return None
    binary_dir = binary_dir.replace("${sourceDir}", REPO_ROOT)
    return os.path.normpath(binary_dir)


# --- Visual Studio / MSVC environment --------------------------------------

def _program_files_x86():
    return os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")


def vs_install_path():
    """Path of the latest Visual Studio install (incl. previews), via vswhere."""
    vswhere = os.path.join(
        _program_files_x86(), "Microsoft Visual Studio", "Installer", "vswhere.exe"
    )
    if not os.path.isfile(vswhere):
        return None
    try:
        result = subprocess.run(
            [vswhere, "-latest", "-prerelease", "-property", "installationPath"],
            capture_output=True, text=True, check=True,
        )
        return result.stdout.strip() or None
    except (OSError, subprocess.SubprocessError):
        return None


def vcvarsall():
    install = vs_install_path()
    if not install:
        return None
    path = os.path.join(install, "VC", "Auxiliary", "Build", "vcvarsall.bat")
    return path if os.path.isfile(path) else None


def needs_msvc_env(generator):
    """True when building needs the MSVC dev environment (vcvars) set up.

    On Windows that's the Visual Studio generators and the command-line MSVC
    generators (Ninja / NMake), which inherit the compiler from the environment.
    Xcode / Unix Makefiles and any non-Windows host never need it.
    """
    if sys.platform != "win32" or not generator:
        return False
    g = generator.lower()
    return "visual studio" in g or "ninja" in g or "nmake" in g


def msvc_env(arch="x64"):
    """Run vcvarsall and capture the resulting environment as a dict (or None)."""
    vc = vcvarsall()
    if not vc:
        return None
    # vcvarsall prints a banner then we dump `set`; a >nul redirect on the .bat
    # makes it return non-zero, so let it print and skip non KEY=VALUE lines.
    result = subprocess.run(
        f'"{vc}" {arch} && set',
        shell=True, capture_output=True, text=True,
    )
    if result.returncode != 0:
        return None
    env = {}
    for line in result.stdout.splitlines():
        # Environment variable names can't contain '=' or spaces; that filters
        # out vcvarsall's banner lines cleanly.
        key, sep, value = line.partition("=")
        if sep and key and " " not in key:
            env[key] = value
    return env if "PATH" in env else None


# --- Tool discovery --------------------------------------------------------

def find_cmake(env=None):
    """Locate cmake on PATH (using env's PATH if given), else inside the VS install."""
    search_path = env.get("PATH") if env else None
    exe = shutil.which("cmake", path=search_path) or shutil.which("cmake")
    if exe:
        return exe
    install = vs_install_path()
    if install:
        candidate = os.path.join(
            install, "Common7", "IDE", "CommonExtensions", "Microsoft",
            "CMake", "CMake", "bin", "cmake.exe",
        )
        if os.path.isfile(candidate):
            return candidate
    return None


def find_clang_format():
    """Locate clang-format on PATH, else the copy bundled with Visual Studio's LLVM."""
    exe = shutil.which("clang-format")
    if exe:
        return exe
    install = vs_install_path()
    if install:
        for sub in (("VC", "Tools", "Llvm", "bin"), ("VC", "Tools", "Llvm", "x64", "bin")):
            candidate = os.path.join(install, *sub, "clang-format.exe")
            if os.path.isfile(candidate):
                return candidate
    return None
