"""Machine-local settings for the Bernini scripts (scripts/config.json).

Three things vary per machine rather than per checkout: which CMake preset you
build, where the toolchain lives when it isn't on PATH, and whether a batch file
has to run first to put a compiler in the environment (vcvarsall.bat on Windows).
config.json records them once so they don't have to be rediscovered -- vswhere
plus a vcvarsall round-trip costs a second or two on every invocation -- or
retyped. `just init` generates it, and it is not committed: it describes a machine,
not the project.

Every key is optional. A missing one falls back to the auto-detection in
cmake_tools, so a fresh clone still builds with no config.json at all.

Precedence, highest first:  command-line flag  >  config.json  >  auto-detection.

Schema:
    preset       CMake preset to configure/build with.
    config       Configuration for multi-config generators, e.g. "Debug".
    arch         vcvars architecture, e.g. "x64". Only used to auto-locate
                 vcvarsall when `precommand` is unset.
    precommand   Shell command run for its effect on the environment before any
                 build; the environment it leaves behind is what child processes
                 get. Typically a quoted vcvarsall.bat plus its architecture.
    tools        Absolute paths to programs that aren't on PATH. Each may name
                 the executable itself or the directory holding it.
                 Recognised: cmake, ninja, clang, clang-format.
                 clang++ is taken from clang's directory.
"""

import json
import os
import sys

import util.cmake_tools as ct

PATH = os.path.join(ct.REPO_ROOT, "scripts", "config.json")

# Used when neither the command line nor config.json names a preset.
DEFAULT_PRESET = (
    "macos-clang-debug" if sys.platform == "darwin" else "windows-vs2026-msvc-dx12-debug"
)

DEFAULT_ARCH = "x64"

TOOL_NAMES = ("cmake", "ninja", "clang", "clang-format")

_cache = None


# --- File ------------------------------------------------------------------

def rel(path):
    """`path` relative to the repo root, for messages the user has to act on."""
    try:
        return os.path.relpath(path, ct.REPO_ROOT)
    except ValueError:  # different drive on Windows
        return path


def load():
    """The parsed config.json, or {} when it is absent, empty or malformed."""
    global _cache
    if _cache is None:
        _cache = _read()
    return _cache


def _read():
    try:
        with open(PATH, encoding="utf-8") as fh:
            text = fh.read().strip()
    except OSError:
        return {}
    if not text:
        return {}
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        print(f"warning: {rel(PATH)} is not valid JSON ({exc}); ignoring it. "
              f"Regenerate it with `just init`.", file=sys.stderr)
        return {}
    return data if isinstance(data, dict) else {}


def save(data):
    global _cache
    with open(PATH, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2)
        fh.write("\n")
    _cache = data


def exists():
    return os.path.isfile(PATH)


# --- Settings --------------------------------------------------------------

def preset(override=None):
    return override or load().get("preset") or DEFAULT_PRESET


def build_config(override=None, generator=None):
    """Configuration for multi-config generators (Debug/Release), or None.

    An explicit override always wins. The recorded default is only handed to
    multi-config generators: a single-config one baked its configuration into the
    cache at configure time, and the recorded value describes the recorded preset,
    which may not be the preset being built (`just build --preset <a-ninja-one>`).
    Pass the generator whenever it's known so that stays true.
    """
    if override:
        return override
    if generator is not None and not ct.is_multi_config(generator):
        return None
    return load().get("config") or None


def artifact_config(override=None):
    """Configuration to select built artifacts for, per the configured preset."""
    return build_config(override, ct.generator_of(preset()))


def arch(override=None):
    return override or load().get("arch") or DEFAULT_ARCH


def precommand():
    """Shell command whose resulting environment every build runs in, or None."""
    return load().get("precommand") or None


def build_dir(override=None):
    """Build directory to read the CMake File API codemodel from.

    An explicit --build-dir wins. Otherwise the configured preset's binaryDir is
    used, so `just run` and friends look at the build you actually build -- but only
    once it has been configured, since before that there's no codemodel there to
    read. Falling back to None lets the caller scan build/* as it always has.
    """
    if override:
        return override
    path = ct.binary_dir_of(preset())
    return path if path and ct.has_reply(path) else None


# --- Tools -----------------------------------------------------------------

def _exe(name):
    return name + ".exe" if sys.platform == "win32" else name


def tool(name):
    """Configured path for tools.<name>, or None when unset or stale.

    The value may be the executable itself or the directory holding it; a
    hand-edited config is likelier to have an LLVM `bin` pasted into it than a
    full path to a specific .exe. A configured path that no longer exists warns
    and resolves to None, so a stale entry degrades to auto-detection instead of
    failing the build.
    """
    value = (load().get("tools") or {}).get(name)
    if not value:
        return None

    path = os.path.expanduser(os.path.expandvars(value))
    if os.path.isdir(path):
        path = os.path.join(path, _exe(name))
    if not os.path.isfile(path):
        print(f"warning: tools.{name} in {rel(PATH)} is '{value}', which does not exist; "
              f"falling back to auto-detection. Re-run `just init` to refresh it.", file=sys.stderr)
        return None
    return path


def find_cmake(env=None):
    return tool("cmake") or ct.find_cmake(env)


def find_ninja(env=None):
    return tool("ninja") or ct.find_ninja(env)


def find_clang_format():
    return tool("clang-format") or ct.find_clang_format()


def find_clang(env=None):
    """{"c": clang, "cxx": clang++} from config, else auto-detected, else None.

    Only `clang` is configured; clang++ is its neighbour in the same directory,
    which is how every LLVM distribution ships and saves naming both.
    """
    c = tool("clang")
    if c:
        cxx = os.path.join(os.path.dirname(c), _exe("clang++"))
        if os.path.isfile(cxx):
            return {"c": c, "cxx": cxx}
        print(f"warning: tools.clang resolved to '{c}' but there is no clang++ beside it; "
              f"falling back to auto-detection.", file=sys.stderr)
    return ct.find_clang(env)


# --- Environment -----------------------------------------------------------

def build_env(generator=None, arch_override=None):
    """The environment to run cmake and the toolchain in, plus a label for --dry-run.

    A `precommand` is run for its environment and that environment is used. With
    none set we fall back to locating vcvarsall via vswhere, but only for the
    generators that need MSVC in the environment -- which is what keeps a fresh
    clone building before `just init` has ever run.

    Returns (env, description).
    """
    pre = precommand()
    if pre:
        captured = ct.capture_env(pre)
        if captured:
            return captured, f"precommand: {pre}"
        print(f"warning: precommand from {rel(PATH)} failed, so the compiler may not be on PATH:\n"
              f"    {pre}\n"
              f"Proceeding with the current environment.", file=sys.stderr)
        return os.environ.copy(), "inherited (precommand failed)"

    if ct.needs_msvc_env(generator):
        want = arch(arch_override)
        captured = ct.msvc_env(want)
        if captured:
            return captured, f"vcvars ({want})"
        print("warning: this generator needs the MSVC environment but vcvarsall.bat was not found "
              "via vswhere.\nSet `precommand` in scripts/config.json (`just init`), or build from a "
              "developer prompt. Proceeding with the current environment.", file=sys.stderr)
        return os.environ.copy(), "inherited (vcvars not found)"

    return os.environ.copy(), "inherited"
