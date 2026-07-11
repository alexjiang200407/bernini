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

Also offers to install `just`, the task runner behind the root justfile. That is
optional -- every recipe is a one-line call into these scripts, so
`python scripts/build.py ...` works with or without it -- which is why this asks
rather than installs.

Bootstrap this with `python scripts/init.py`; afterwards it is `just init`.

Usage:
    just init                                       # detect, prompt, write
    just init --preset windows-clang-dx12-debug     # skip the preset prompt
    just init --show                                # print what would be written
    just init --force                               # overwrite without confirming
    just init --no-just                             # skip the `just` check
"""

import argparse
import os
import shutil
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg

REQUIREMENTS = os.path.join(ct.REPO_ROOT, "scripts", "requirements.txt")


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
    return 0


if __name__ == "__main__":
    sys.exit(main())
