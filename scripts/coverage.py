#!/usr/bin/env python3
"""Build the coverage preset, run the test suites instrumented, and report what they executed.

Coverage is a diagnostic, not a gate: the output is a per-file summary on stdout and an
HTML report, saying which source lines the suites reached. macOS only. docs/coverage.md
covers the build mode itself and how to drive llvm-cov by hand.

Usage:
    python scripts/coverage.py                     # every suite
    python scripts/coverage.py core assetlib       # only suites matching these names
    python scripts/coverage.py --no-build          # run what is already built
    python scripts/coverage.py -j 1                # one process per suite, output streamed live
    python scripts/coverage.py --preset <name>     # a coverage preset other than the default
    python scripts/coverage.py -- "[readback]"     # forward a Catch2 filter to every suite

The suites run through run_tests.py against the coverage build directory, so discovery,
sharding and cwd handling behave exactly like `just test`. Each process writes its own
profile (`%p` -- the shards of one suite would otherwise overwrite a single file), the
profiles are merged, and the report hands llvm-cov every executable and shared library
as an -object: llvm-cov silently omits any image it is not given, and nearly all of
libs/bgl lives in libbgl.dylib.

The report is filtered to our own source roots. Everything else the profile touches --
vcpkg headers, FetchContent checkouts, generated code -- is somebody else's number.

A failing suite does not stop the report: coverage of what did run is still real, and
the exit code carries the failure.
"""

import argparse
import glob
import os
import subprocess
import sys

import util.cmake_tools as ct

from tidy import SOURCE_ROOTS

# Not config.json's `preset`: that names the machine's dev preset, whose build is
# deliberately uninstrumented -- running the suites there would write no profiles at
# all. The coverage preset is project configuration (CMakePresets.json), so it is a
# constant here, overridable with --preset like build.py's.
DEFAULT_PRESET = "macos-clang-metal-coverage"


def xcrun(tool, args):
    """Run an LLVM coverage tool via xcrun, so it matches the toolchain that compiled.

    A profile is only readable by a matching llvm-profdata, and xcrun resolves against
    the active developer directory -- the same place the build's clang came from.
    """
    return subprocess.run(["xcrun", tool, *args])


def instrumented_images(build_dir):
    """Every executable and shared-library artifact on disk, each a coverage -object.

    Enumerated from the codemodel rather than naming bgl, so a second shared library
    is picked up without touching this code. Static libraries are skipped: their
    objects are embedded in the executables that link them, and llvm-cov dedupes the
    shared function records.
    """
    images = []
    for target in ct.load_targets(build_dir):
        if target["type"] not in ("EXECUTABLE", "SHARED_LIBRARY"):
            continue
        for artifact in target["artifacts"]:
            if os.path.isfile(artifact) and artifact not in images:
                images.append(artifact)
    return images


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("filters", nargs="*",
                        help="Substrings of suite names to run (default: all of them).")
    parser.add_argument("--no-build", action="store_true",
                        help="Don't build first; run the binaries that are already there.")
    parser.add_argument("--preset", default=DEFAULT_PRESET,
                        help=f"Coverage preset to build and report (default: {DEFAULT_PRESET}).")
    parser.add_argument("-j", "--jobs", type=int,
                        help="Shards per suite, forwarded to the test runner.")

    argv = sys.argv[1:]
    forward = []
    if "--" in argv:
        split = argv.index("--")
        argv, forward = argv[:split], argv[split + 1:]
    args = parser.parse_args(argv)

    if sys.platform != "darwin":
        print("error: coverage is a macOS capability -- MSVC has no source-based coverage. "
              "See docs/coverage.md.", file=sys.stderr)
        return 1

    if not args.no_build:
        rc = subprocess.run(
            [sys.executable, os.path.join(ct.REPO_ROOT, "scripts", "build.py"),
             "--preset", args.preset]).returncode
        if rc:
            print(f"build failed (exit {rc}); running no tests.", file=sys.stderr)
            return rc

    build_dir = ct.binary_dir_of(args.preset)
    if not build_dir or not ct.has_reply(build_dir):
        print(f"error: '{args.preset}' has not been configured. Build it first:\n"
              f"    just build --preset {args.preset}", file=sys.stderr)
        return 2

    coverage_dir = os.path.join(build_dir, "coverage")
    profile_dir = os.path.join(coverage_dir, "profiles")
    os.makedirs(profile_dir, exist_ok=True)

    # Leftover profiles would silently mix a previous run into the merge.
    for stale in glob.glob(os.path.join(profile_dir, "*.profraw")):
        os.remove(stale)

    env = os.environ.copy()
    env["LLVM_PROFILE_FILE"] = os.path.join(profile_dir, "bernini-%p.profraw")

    run_cmd = [sys.executable, os.path.join(ct.REPO_ROOT, "scripts", "run_tests.py"),
               "--no-build", "--build-dir", build_dir, *args.filters]
    if args.jobs:
        run_cmd += ["--jobs", str(args.jobs)]
    if forward:
        run_cmd += ["--", *forward]
    suites_rc = subprocess.run(run_cmd, env=env).returncode

    profiles = sorted(glob.glob(os.path.join(profile_dir, "*.profraw")))
    if not profiles:
        # A runner that failed before launching anything already printed the real error.
        if suites_rc:
            return suites_rc
        print("error: the suites wrote no profiles. Was the coverage preset built with "
              "BUILD_COVERAGE on?", file=sys.stderr)
        return 1

    merged = os.path.join(coverage_dir, "bernini.profdata")
    rc = xcrun("llvm-profdata", ["merge", "-sparse", *profiles, "-o", merged]).returncode
    if rc:
        print(f"llvm-profdata merge failed (exit {rc}).", file=sys.stderr)
        return rc

    images = instrumented_images(build_dir)
    if not images:
        print("error: no executables or shared libraries found in the codemodel.",
              file=sys.stderr)
        return 1
    objects = [images[0]]
    for image in images[1:]:
        objects += ["-object", image]

    roots = [os.path.join(ct.REPO_ROOT, r) for r in SOURCE_ROOTS
             if os.path.isdir(os.path.join(ct.REPO_ROOT, r))]

    html_dir = os.path.join(coverage_dir, "html")
    rc = xcrun("llvm-cov", ["show", "-format=html", f"-output-dir={html_dir}",
                            *objects, f"-instr-profile={merged}", *roots]).returncode
    if rc:
        print(f"llvm-cov show failed (exit {rc}).", file=sys.stderr)
        return rc

    rc = xcrun("llvm-cov", ["report", *objects, f"-instr-profile={merged}", *roots]).returncode
    if rc:
        print(f"llvm-cov report failed (exit {rc}).", file=sys.stderr)
        return rc

    print(f"\nHTML report: {os.path.join(html_dir, 'index.html')}")
    if suites_rc:
        print("note: at least one suite failed; the report covers what did run.",
              file=sys.stderr)
    return suites_rc


if __name__ == "__main__":
    sys.exit(main())
