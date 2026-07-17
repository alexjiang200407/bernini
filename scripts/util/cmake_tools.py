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


def has_reply(build_dir):
    """True when this build dir holds a File API reply, i.e. it has been configured."""
    return bool(glob.glob(os.path.join(_reply_dir(build_dir), "index-*.json")))


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


def is_multi_config(generator):
    """True for generators that pick Debug/Release at build time, not configure time."""
    if not generator:
        return False
    g = generator.lower()
    return "visual studio" in g or "xcode" in g


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


def host_system_name():
    """The value CMake would give ${hostSystemName} here."""
    return {"win32": "Windows", "darwin": "Darwin"}.get(sys.platform, "Linux")


def runs_on_host(preset):
    """False when the preset's `condition` excludes this machine.

    Only the hostSystemName equality form the presets actually use is understood;
    anything else is treated as satisfied and left for CMake to reject.
    """
    _, by_name = load_presets()
    condition = _resolve_field(preset, by_name, "condition")
    if not isinstance(condition, dict):
        return True
    if condition.get("type") != "equals" or condition.get("lhs") != "${hostSystemName}":
        return True
    return condition.get("rhs") == host_system_name()


def _resolve_cache_var(name, by_name, key, seen=None):
    """First value of cache variable `key`, searching the preset then its inherits.

    Mirrors CMake preset precedence: the preset itself wins over what it inherits,
    and earlier entries in `inherits` win over later ones (depth-first).
    """
    seen = seen or set()
    if name in seen or name not in by_name:
        return None
    seen.add(name)
    preset = by_name[name]
    cache = preset.get("cacheVariables", {})
    if key in cache:
        value = cache[key]
        # A cache var may be a bare string or {"type": ..., "value": ...}.
        return value.get("value") if isinstance(value, dict) else value
    inherits = preset.get("inherits", [])
    if isinstance(inherits, str):
        inherits = [inherits]
    for parent in inherits:
        value = _resolve_cache_var(parent, by_name, key, seen)
        if value is not None:
            return value
    return None


def cache_var_of(preset, key):
    """Resolve a cache variable's value through the preset's inherit chain (or None)."""
    _, by_name = load_presets()
    return _resolve_cache_var(preset, by_name, key)


def uses_clang(preset):
    """True when the preset's CXX compiler resolves to some flavor of clang."""
    compiler = cache_var_of(preset, "CMAKE_CXX_COMPILER") or ""
    return "clang" in os.path.basename(str(compiler)).lower()


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


def capture_env(command):
    """Run `command` in a shell and capture the environment it leaves behind (or None).

    A script like vcvarsall.bat only mutates the shell it runs in, so the way to
    apply it is to run it, dump the environment afterwards, and hand the result to
    our own child processes. This is what a `precommand` in scripts/config.json
    goes through.
    """
    dump = "set" if sys.platform == "win32" else "env"
    # The command may print a banner before we dump; a `>nul` redirect on
    # vcvarsall.bat makes it return non-zero, so let it print and drop any line
    # that isn't KEY=VALUE.
    result = subprocess.run(
        f"{command} && {dump}",
        shell=True, capture_output=True, text=True,
    )
    if result.returncode != 0:
        return None
    env = {}
    for line in result.stdout.splitlines():
        # Environment variable names can't contain '=' or spaces; that filters
        # out the banner lines cleanly.
        key, sep, value = line.partition("=")
        if sep and key and " " not in key:
            env[key] = value
    # cmd.exe echoes a variable under its stored casing, so PATH may come back as
    # `Path`. Its absence means we captured a banner rather than an environment.
    return env if any(k.upper() == "PATH" for k in env) else None


def msvc_env(arch="x64"):
    """Environment left behind by vcvarsall.bat, located via vswhere (or None)."""
    vc = vcvarsall()
    if not vc:
        return None
    return capture_env(f'"{vc}" {arch}')


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


def find_clang(env=None):
    """Locate a clang / clang++ pair for use as the CMake compiler.

    Prefers the "C++ Clang tools for Windows" (LLVM) component bundled with
    Visual Studio -- the one installed from the VS Installer -- and falls back to
    clang on PATH (env's PATH if given) otherwise. Returns
    {"c": <clang>, "cxx": <clang++>} or None if a matching pair isn't found.
    """
    install = vs_install_path()
    if install:
        # Prefer the 64-bit hosted toolchain when both are present.
        for sub in (("VC", "Tools", "Llvm", "x64", "bin"), ("VC", "Tools", "Llvm", "bin")):
            base = os.path.join(install, *sub)
            c = os.path.join(base, "clang.exe")
            cxx = os.path.join(base, "clang++.exe")
            if os.path.isfile(c) and os.path.isfile(cxx):
                return {"c": c, "cxx": cxx}
    search_path = env.get("PATH") if env else None
    c = shutil.which("clang", path=search_path) or shutil.which("clang")
    cxx = shutil.which("clang++", path=search_path) or shutil.which("clang++")
    if c and cxx:
        return {"c": c, "cxx": cxx}
    return None


def find_ninja(env=None):
    """Locate ninja on PATH (env's PATH if given), else the copy bundled with VS's CMake."""
    search_path = env.get("PATH") if env else None
    exe = shutil.which("ninja", path=search_path) or shutil.which("ninja")
    if exe:
        return exe
    install = vs_install_path()
    if install:
        candidate = os.path.join(
            install, "Common7", "IDE", "CommonExtensions", "Microsoft",
            "CMake", "Ninja", "ninja.exe",
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
