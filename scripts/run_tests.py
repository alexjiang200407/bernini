#!/usr/bin/env python3
"""Build and run every test suite, and report which of them passed.

The suites are discovered, not listed: any executable target whose name ends in `_tests`
is one, so a new suite is picked up by adding the target, with nothing to update here.

Usage:
    python scripts/run_tests.py                    # every suite
    python scripts/run_tests.py bgl editor         # only suites matching these names
    python scripts/run_tests.py --list             # name them without running anything
    python scripts/run_tests.py --no-build         # run whatever is already built
    python scripts/run_tests.py --config Release
    python scripts/run_tests.py -- "[readback]"    # forward a Catch filter to every suite
    python scripts/run_tests.py bgl -- "[readback]"  # suite-name filter + forwarded Catch filter

Everything after `--` is forwarded verbatim to each suite binary. Every suite is Catch2, so
this is how one tag or name filter reaches all of them at once -- useful when a backend
implements only part of the suite and the rest is selected by tag.

Each suite runs with cwd set to its output directory, because the binaries resolve asset
paths relative to it.

Output is *not* captured. A suite writes straight to this console, both because a long
one should show progress rather than go quiet for minutes, and because Qt emits QTest's
results through a path a Windows pipe swallows -- capturing editor_tests would silently
turn a real run into a blank one.

To pass arguments to a single suite, use `just run` instead, which also forwards them:

    just run bgl_tests -- --gpu-validation
"""

import argparse
import os
import subprocess
import sys
import time

import util.cmake_tools as ct
import util.config as cfg

# What makes a target a test suite.
SUITE_SUFFIX = "_tests"


def find_suites(build_dirs, config):
    """Every test executable, as {name: path}, preferring an artifact that exists on disk."""
    suites = {}
    for build_dir in build_dirs:
        for target in ct.load_targets(build_dir):
            if target["type"] != "EXECUTABLE" or not target["name"].endswith(SUITE_SUFFIX):
                continue
            if config and target["config"].lower() != config.lower():
                continue

            artifacts = sorted(target["artifacts"], key=lambda p: not os.path.isfile(p))
            if artifacts:
                suites.setdefault(target["name"], artifacts[0])

    return dict(sorted(suites.items()))


def select(suites, filters):
    """The suites whose name contains any of `filters` (all of them when there are none)."""
    if not filters:
        return suites

    chosen = {
        name: path
        for name, path in suites.items()
        if any(f.lower() in name.lower() for f in filters)
    }
    return chosen


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("filters", nargs="*",
                        help="Substrings of suite names to run (default: all of them).")
    parser.add_argument("--build-dir", help="Build directory (default: the configured preset's).")
    parser.add_argument("--config", help="Configuration (e.g. Debug, Release; default: config.json).")
    parser.add_argument("--no-build", action="store_true",
                        help="Don't build first; run the binaries that are already there.")
    parser.add_argument("--list", action="store_true", help="Name the suites without running them.")

    # Everything after `--` is forwarded verbatim to every suite binary. Each suite is Catch2, so
    # this is how a tag or name filter reaches all of them at once: `just test -- "[readback]"`.
    # `just test bgl -- "[readback]"` combines a suite-name filter with a forwarded Catch filter.
    argv = sys.argv[1:]
    forward = []
    if "--" in argv:
        split = argv.index("--")
        argv, forward = argv[:split], argv[split + 1:]
    args = parser.parse_args(argv)

    # Build everything before resolving: a build dir that has never been configured has no
    # codemodel to discover the suites in, and building is what produces one.
    if not args.no_build and not args.list:
        build_cmd = [sys.executable, os.path.join(ct.REPO_ROOT, "scripts", "build.py")]
        if args.config:
            build_cmd += ["--config", args.config]
        rc = subprocess.run(build_cmd).returncode
        if rc:
            print(f"build failed (exit {rc}); running no tests.", file=sys.stderr)
            return rc

    build_dirs = ct.find_build_dirs(cfg.build_dir(args.build_dir))
    if not build_dirs:
        print("No CMake File API codemodel found. Configure the build first:\n"
              "    just build --configure", file=sys.stderr)
        return 2

    suites = find_suites(build_dirs, cfg.artifact_config(args.config))
    if not suites:
        print("No test suites found. They are built only when BUILD_TESTS is on, which the\n"
              "debug presets set and the release ones do not.", file=sys.stderr)
        return 1

    chosen = select(suites, args.filters)
    if not chosen:
        print(f"error: no test suite matches {' '.join(args.filters)}. There is: "
              + ", ".join(suites), file=sys.stderr)
        return 1

    if args.list:
        for name in chosen:
            print(name)
        return 0

    results = []
    for name, exe in chosen.items():
        if not os.path.isfile(exe):
            hint = f"Build it with: just build {name}" if args.no_build \
                else "The build reported success but wrote no binary there."
            print(f"error: '{exe}' is not built. {hint}", file=sys.stderr)
            results.append((name, 1, 0.0))
            continue

        print(f"\n=== {name} ===", flush=True)

        started = time.monotonic()
        rc = subprocess.run([exe, *forward], cwd=os.path.dirname(exe)).returncode
        results.append((name, rc, time.monotonic() - started))

    # A failing suite does not stop the others: one full report beats finding out about the
    # next failure only after fixing this one.
    failed = [name for name, rc, _ in results if rc != 0]

    print("\n=== summary ===")
    width = max(len(name) for name, _, _ in results)
    for name, rc, seconds in results:
        status = "ok" if rc == 0 else f"FAILED (exit {rc})"
        print(f"{name:<{width}}  {seconds:6.1f}s  {status}")

    if failed:
        print(f"\n{len(failed)} of {len(results)} suites failed: {', '.join(failed)}")
        return 1

    print(f"\nall {len(results)} suites passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
