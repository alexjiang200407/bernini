#!/usr/bin/env python3
"""Configure and build a CMake target via presets (generator-agnostic).

Usage:
    python scripts/build.py                       # build everything (default preset)
    python scripts/build.py bgl_tests             # build one target
    python scripts/build.py bgl --preset windows-ninja-msvc-dx12-debug
    python scripts/build.py --preset windows-clang-dx12-debug   # clang + Ninja
    python scripts/build.py --config Release      # multi-config generators
    python scripts/build.py --configure           # configure only, don't build
    python scripts/build.py --dry-run             # print the plan, don't run

The MSVC developer environment (vcvars) is set up automatically when the
preset's generator needs it (Visual Studio, Ninja, NMake on Windows); other
generators (Xcode, Unix Makefiles) are left untouched.

Ninja and clang presets get their make program and compiler pinned to absolute
paths: ninja and the clang/clang++ pair are resolved from the Visual Studio
install (its bundled Ninja and "C++ Clang tools for Windows" LLVM component) when
they aren't on PATH, so a clang build works without extra shell setup.
"""

import argparse
import os
import subprocess
import sys

import util.cmake_tools as ct

DEFAULT_PRESET = "windows-vs2026-msvc-dx12-debug"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", nargs="?", help="Target to build (default: all).")
    parser.add_argument("--preset", default=DEFAULT_PRESET, help=f"CMake preset (default: {DEFAULT_PRESET}).")
    parser.add_argument("--config", help="Build configuration for multi-config generators (e.g. Debug, Release).")
    parser.add_argument("--arch", default="x64", help="vcvars architecture (default: x64).")
    configure_group = parser.add_mutually_exclusive_group()
    configure_group.add_argument("--configure", action="store_true", help="Configure only; skip the build step.")
    configure_group.add_argument("--no-configure", action="store_true", help="Skip the configure step (build only).")
    parser.add_argument("--dry-run", action="store_true", help="Print what would run without executing.")
    args = parser.parse_args()

    generator = ct.generator_of(args.preset)
    if generator is None:
        print(f"warning: could not resolve a generator for preset '{args.preset}'.", file=sys.stderr)

    wants_msvc = ct.needs_msvc_env(generator)

    env = os.environ.copy()
    env_source = "inherited"
    if wants_msvc and not args.dry_run:
        msvc = ct.msvc_env(args.arch)
        if msvc is None:
            print("warning: MSVC environment is needed but vcvarsall.bat was not found via vswhere. "
                  "Proceeding with the current environment.", file=sys.stderr)
        else:
            env = msvc
            env_source = f"vcvars ({args.arch})"

    cmake = ct.find_cmake(env)
    if not cmake:
        print("error: cmake not found on PATH or in the Visual Studio install.", file=sys.stderr)
        return 1

    configure_cmd = [cmake, "--preset", args.preset]
    build_cmd = [cmake, "--build", "--preset", args.preset]

    # Resolve toolchain programs that may not be on PATH and pin them as absolute
    # paths so the configure works regardless of how the shell is set up:
    #   * Ninja generators need a make program (VS bundles one off-PATH).
    #   * clang presets need clang/clang++ (VS LLVM component preferred, else PATH).
    toolchain = []
    if generator and "ninja" in generator.lower():
        ninja = ct.find_ninja(env)
        if ninja:
            configure_cmd += [f"-DCMAKE_MAKE_PROGRAM={ninja}"]
            toolchain.append(f"ninja: {ninja}")
        else:
            print("warning: Ninja generator selected but ninja was not found on PATH "
                  "or in the Visual Studio install.", file=sys.stderr)
    if ct.uses_clang(args.preset):
        clang = ct.find_clang(env)
        if clang:
            configure_cmd += [f"-DCMAKE_C_COMPILER={clang['c']}",
                              f"-DCMAKE_CXX_COMPILER={clang['cxx']}"]
            toolchain.append(f"clang: {clang['cxx']}")
        else:
            print("warning: clang preset selected but clang/clang++ were not found. "
                  "Install the \"C++ Clang tools for Windows\" (LLVM) component from the "
                  "Visual Studio Installer, or add clang to your PATH.", file=sys.stderr)

    if args.target:
        build_cmd += ["--target", args.target]
    if args.config:
        build_cmd += ["--config", args.config]

    if args.dry_run:
        print(f"preset:    {args.preset}")
        print(f"generator: {generator}")
        print(f"cmake:     {cmake}")
        print(f"env:       {'vcvars (' + args.arch + ')' if wants_msvc else env_source}")
        for note in toolchain:
            print(f"toolchain: {note}")
        if not args.no_configure:
            print("configure: " + " ".join(configure_cmd))
        if not args.configure:
            print("build:     " + " ".join(build_cmd))
        return 0

    if not args.no_configure:
        rc = subprocess.run(configure_cmd, env=env).returncode
        if rc:
            print(f"configure failed (exit {rc}).", file=sys.stderr)
            return rc

    if args.configure:
        return 0

    return subprocess.run(build_cmd, env=env).returncode


if __name__ == "__main__":
    sys.exit(main())
