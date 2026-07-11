#!/usr/bin/env python3
"""Run a built executable target, with cwd set to its output directory.

The output directory matters: binaries here resolve asset paths relative to the
working directory, so this always runs from the folder containing the exe.

Usage:
    python scripts/exec_target.py bgl_tests
    python scripts/exec_target.py bgl_tests -- --list-tests   # args after --
    python scripts/exec_target.py bgl_tests --config Release
    python scripts/exec_target.py bgl_tests --dry-run

The target path is resolved from the File API codemodel (generator-agnostic);
build it first if it doesn't exist yet. Which build dir and configuration to look
in default to the preset recorded in scripts/config.json (see `just init`).
"""

import argparse
import os
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", help="Executable target name.")
    parser.add_argument("--build-dir", help="Build directory (default: the configured preset's).")
    parser.add_argument("--config", help="Configuration to run (e.g. Debug, Release; default: config.json).")
    parser.add_argument("--dry-run", action="store_true", help="Print the command and cwd without running.")
    parser.epilog = "Arguments for the executable itself go after a literal `--`."

    # Split our own options from the program's args on the first `--` so that
    # flags like `--list-tests` are forwarded instead of parsed by us.
    argv = sys.argv[1:]
    if "--" in argv:
        split = argv.index("--")
        own_args, passthrough = argv[:split], argv[split + 1:]
    else:
        own_args, passthrough = argv, []
    args = parser.parse_args(own_args)

    config = cfg.artifact_config(args.config)

    build_dirs = ct.find_build_dirs(cfg.build_dir(args.build_dir))
    if not build_dirs:
        print("No CMake File API codemodel found. Configure the build first:\n"
              "    just build --configure", file=sys.stderr)
        return 2

    candidates = []
    for build_dir in build_dirs:
        for target in ct.load_targets(build_dir):
            if target["name"] != args.target:
                continue
            if config and target["config"].lower() != config.lower():
                continue
            if target["type"] != "EXECUTABLE":
                print(f"error: target '{args.target}' is {target['type']}, not an executable.", file=sys.stderr)
                return 1
            candidates.extend(target["artifacts"])

    if not candidates:
        print(f"error: no executable target named '{args.target}'"
              + (f" for config '{config}'." if config else "."), file=sys.stderr)
        return 1

    # Prefer an artifact that actually exists on disk.
    candidates.sort(key=lambda p: not os.path.isfile(p))
    exe = candidates[0]
    if not os.path.isfile(exe):
        print(f"error: '{exe}' is not built yet. Build it with: b build {args.target}", file=sys.stderr)
        return 1

    cwd = os.path.dirname(exe)
    if args.dry_run:
        print(f"cwd: {cwd}")
        print("cmd: " + " ".join([exe] + passthrough))
        return 0

    return subprocess.run([exe] + passthrough, cwd=cwd).returncode


if __name__ == "__main__":
    sys.exit(main())
