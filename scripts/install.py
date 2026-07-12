#!/usr/bin/env python3
"""Build the CLI tools and stage them, with their DLLs, into a directory you can put on PATH.

Only assetlib_cli is installed. It is the one binary that takes explicit paths and reads
nothing relative to its working directory, so it is the only one that makes sense to run
from outside the build tree; the editor and the examples resolve `assets/` from the cwd
and must be launched from their output dir (`just run`).

The install gathers the exe plus just the DLLs it actually imports -- not the whole build
output, which also holds Qt, SDL and the D3D12/DXC stack that has no business on a PATH the
rest of the machine can see.

Default prefix is <repo>/dist (set in the root CMakeLists), which is stable: unlike a build
dir it is not per-preset and does not vanish when you wipe the build.

Usage:
    just install                        # build assetlib_cli, stage it into ./dist
    just install --prefix D:/tools      # somewhere else
    just install --no-build             # stage whatever is already built
    just install --dry-run              # print the plan
"""

import argparse
import os
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg

TARGET = "assetlib_cli"

# One stable directory to put on PATH. A build dir would not do: it is per-preset and is wiped.
DEFAULT_PREFIX = os.path.join(ct.REPO_ROOT, "dist")

# Our install rules live in this component. Installing it alone keeps the vendored QtNodes -- which
# ships its own install rules -- from dropping its headers and CMake config into the prefix.
COMPONENT = "tools"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--prefix", help=f"Install directory (default: {DEFAULT_PREFIX}).")
    parser.add_argument("--config", help="Configuration to install (default: config.json).")
    parser.add_argument("--no-build", action="store_true", help="Don't build first; install what is there.")
    parser.add_argument("--dry-run", action="store_true", help="Print what would run without executing.")
    args = parser.parse_args()

    preset = cfg.preset()
    generator = ct.generator_of(preset)
    config = cfg.build_config(args.config, generator)

    binary_dir = ct.binary_dir_of(preset)
    if not binary_dir:
        print(f"error: preset '{preset}' has no binaryDir.", file=sys.stderr)
        return 1

    build_cmd = None
    if not args.no_build:
        build_cmd = [sys.executable, os.path.join(ct.REPO_ROOT, "scripts", "build.py"), TARGET]
        if args.config:
            build_cmd += ["--config", args.config]

    env, _env_source = (None, None) if args.dry_run else cfg.build_env(generator)

    cmake = cfg.find_cmake(env)
    if not cmake:
        print("error: cmake not found. Run `just init`.", file=sys.stderr)
        return 1

    # Always explicit: CMAKE_INSTALL_PREFIX is cached from the first configure, so relying on a
    # default set in CMakeLists would silently fall back to C:/Program Files on an existing build dir.
    prefix = os.path.abspath(args.prefix) if args.prefix else DEFAULT_PREFIX

    install_cmd = [cmake, "--install", binary_dir, "--prefix", prefix, "--component", COMPONENT]
    if config:
        install_cmd += ["--config", config]

    if args.dry_run:
        if build_cmd:
            print("build:   " + " ".join(build_cmd))
        print("install: " + " ".join(install_cmd))
        return 0

    if build_cmd:
        rc = subprocess.run(build_cmd).returncode
        if rc:
            print(f"build failed (exit {rc}); nothing installed.", file=sys.stderr)
            return rc

    rc = subprocess.run(install_cmd, env=env).returncode
    if rc:
        print(f"install failed (exit {rc}).", file=sys.stderr)
        return rc

    print(f"\nstaged into {prefix}")
    print(f"Add that directory to PATH to use `{TARGET}` from anywhere.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
