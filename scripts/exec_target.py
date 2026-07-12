#!/usr/bin/env python3
"""Build an executable target and run it, with cwd set to its output directory.

The target is brought up to date first, so `just run <target>` never launches a stale
binary. An incremental no-op build costs about a second (build.py skips the configure
step once a build dir is configured); pass --no-build to skip it.

The output directory matters: binaries here resolve asset paths relative to the
working directory, so this always runs from the folder containing the exe.

Usage:
    python scripts/exec_target.py bgl_tests
    python scripts/exec_target.py bgl_tests -- --list-tests   # args after --
    python scripts/exec_target.py bgl_tests --config Release
    python scripts/exec_target.py bgl_tests --no-build        # run whatever is there
    python scripts/exec_target.py bgl_tests --dry-run

The target path is resolved from the File API codemodel (generator-agnostic). Which
build dir and configuration to look in default to the preset recorded in
scripts/config.json (see `just init`).
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
    parser.add_argument("--no-build", action="store_true",
                        help="Don't build first; run the binary that is already there.")
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

    build_cmd = None
    if not args.no_build:
        build_cmd = [sys.executable, os.path.join(ct.REPO_ROOT, "scripts", "build.py"), args.target]
        if args.config:
            build_cmd += ["--config", args.config]

    # Build before resolving, not after: a build dir that has never been configured has no
    # codemodel to resolve against, and building is what produces one.
    if build_cmd and not args.dry_run:
        rc = subprocess.run(build_cmd).returncode
        if rc:
            print(f"build failed (exit {rc}); not running '{args.target}'.", file=sys.stderr)
            return rc

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
    if not os.path.isfile(exe) and not args.dry_run:
        # With --no-build this is just "you never built it"; otherwise the build above claimed
        # success yet produced nothing at the path the codemodel names, which is worth saying.
        hint = (f"Build it with: just build {args.target}" if args.no_build
                else "The build reported success but wrote no binary there.")
        print(f"error: '{exe}' is not built. {hint}", file=sys.stderr)
        return 1

    cwd = os.path.dirname(exe)
    if args.dry_run:
        if build_cmd:
            print("build: " + " ".join(build_cmd))
        print(f"cwd:   {cwd}")
        print("cmd:   " + " ".join([exe] + passthrough))
        return 0

    return subprocess.run([exe] + passthrough, cwd=cwd).returncode


if __name__ == "__main__":
    sys.exit(main())
