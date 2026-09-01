#!/usr/bin/env python3
"""Configure and build a CMake target via presets (generator-agnostic).

Usage:
    python scripts/build.py                       # build everything (configured preset)
    python scripts/build.py bgl_extended_tests             # build one target
    python scripts/build.py bgl --preset windows-ninja-msvc-dx12-debug
    python scripts/build.py --preset windows-clang-dx12-debug   # clang + Ninja
    python scripts/build.py --config Release      # multi-config generators
    python scripts/build.py --configure           # force a configure, don't build
    python scripts/build.py --dry-run             # print the plan, don't run

The configure step is skipped once the build dir has been configured: the generated
buildsystem re-runs cmake on its own when a CMakeLists changes, and -- because every
glob here is CONFIGURE_DEPENDS -- also when a glob picks up a new or deleted source
file. See needs_configure() for the cases it cannot notice, which we still handle.

The preset, the build configuration and the toolchain paths all come from
scripts/config.json (see `just init`) unless overridden on the command line.

The compiler environment comes from config.json's `precommand` -- normally
vcvarsall.bat. Without one, vcvars is located via vswhere for the generators that
need it (Visual Studio, Ninja, NMake on Windows); Xcode and Unix Makefiles are
left untouched.

VCPKG_ROOT is exported into that environment from the vcpkg recorded in config.json
(else an auto-detected one), so the presets' toolchain file resolves without the
variable being set on the machine.

Ninja and clang presets get their make program and compiler pinned to absolute
paths: ninja and the clang/clang++ pair come from config.json, else from the
Visual Studio install (its bundled Ninja and "C++ Clang tools for Windows" LLVM
component), else from PATH -- so a clang build works without extra shell setup.
"""

import argparse
import hashlib
import os
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg
import util.jobserver as jobserver

# Digest of the CMakePresets.json a build dir was last configured from, written beside its cache.
PRESETS_STAMP = ".bernini-presets.sha256"


def presets_digest():
    """Content hash of CMakePresets.json, or None if it cannot be read."""
    try:
        with open(os.path.join(ct.REPO_ROOT, "CMakePresets.json"), "rb") as fh:
            return hashlib.sha256(fh.read()).hexdigest()
    except OSError:
        return None


def needs_configure(binary_dir):
    """Whether cmake must run before the build can. Returns (bool, reason).

    A configured build dir regenerates itself: the generated buildsystem carries a rule
    (Ninja's build.ninja rule, MSBuild's ZERO_CHECK) that re-runs cmake whenever a CMakeLists
    it read has changed. Every file(GLOB_RECURSE) in this project passes CONFIGURE_DEPENDS, so
    that rule also re-globs and re-runs cmake when a glob's *result* changes -- a new or deleted
    source file is picked up on its own, even though no CMakeLists was touched. `cmake --build`
    therefore already configures exactly when it has to, and running `cmake --preset` in front of
    it only pays for the same check twice.

    That leaves the cases the buildsystem cannot notice for itself:
      * there is no buildsystem yet -- a fresh or wiped dir, or a preset whose dir is unbuilt,
      * the File API codemodel is missing, which the discovery scripts need,
      * CMakePresets.json changed. Presets are consumed by the cmake CLI at configure time and
        never enter the generated buildsystem, so an edited cacheVariable would otherwise be
        silently ignored until somebody reconfigured by hand.

    The preset check compares content, not mtime: git rewrites the file on checkout, stash and
    branch switch, so an mtime would force a full reconfigure every time you moved between
    branches even though nothing in it changed.
    """
    if not binary_dir:
        return True, "the preset has no binaryDir"

    if not os.path.isfile(os.path.join(binary_dir, "CMakeCache.txt")):
        return True, "not configured yet"

    if not ct.has_reply(binary_dir):
        return True, "no File API codemodel"

    try:
        with open(os.path.join(binary_dir, PRESETS_STAMP), encoding="utf-8") as fh:
            stamped = fh.read().strip()
    except OSError:
        stamped = None

    if stamped != presets_digest():
        return True, "CMakePresets.json changed since the last configure"

    return False, "already configured (cmake reconfigures itself if a CMakeLists or a glob changed)"


def write_presets_stamp(binary_dir):
    """Record the presets a successful configure was driven by, for needs_configure()."""
    digest = presets_digest()
    if not binary_dir or not digest:
        return
    try:
        with open(os.path.join(binary_dir, PRESETS_STAMP), "w", encoding="utf-8") as fh:
            fh.write(digest + "\n")
    except OSError as exc:
        # Only costs a redundant configure next time; never worth failing a good build over.
        print(f"warning: could not write {PRESETS_STAMP}: {exc}", file=sys.stderr)


def repair_deps_log(ninja, binary_dir, env):
    """
    Rewrites Ninja's dependency log, reporting when it had to be recovered rather than merely
    compacted.

    A build killed part-way -- Ctrl-C, a pipe closed by `head`, a machine that went down -- leaves
    `.ninja_deps` truncated. Ninja recovers by discarding everything past the tear, but an object
    whose entry went with it has no recorded dependencies, and Ninja must rebuild it to find out what
    they are. From then on every build rebuilds it, and the only symptom is one warning line that
    scrolls past: `premature end of file; recovering`.

    Recompacting is what actually repairs it, and against a log of several megabytes it costs tens of
    milliseconds -- less than probing for the damage separately, since a probe has to read the same
    file. So it runs before every Ninja build rather than after a diagnosis.
    """
    if not (ninja and binary_dir):
        return

    if not os.path.exists(os.path.join(binary_dir, ".ninja_deps")):
        return

    result = subprocess.run([ninja, "-C", binary_dir, "-t", "recompact"],
                            env=env, capture_output=True, text=True)

    if result.returncode:
        # Nothing here is worth failing a build over: at worst the log stays as it was. The reason
        # travels with it, since this is the one place that captured ninja's stderr.
        detail = (result.stderr or "").strip()
        print(f"warning: could not compact ninja's dependency log (exit {result.returncode})."
              + (f"\n{detail}" if detail else ""), file=sys.stderr, flush=True)
        return

    if "premature end of file" in (result.stdout or "") + (result.stderr or ""):
        # Flushed, because the build below writes straight to the terminal and would otherwise
        # come out ahead of a note that describes what happened before it.
        print("note: ninja's dependency log was truncated and has been repaired. Until now every "
              "build was recompiling the objects whose recorded dependencies went with it.",
              flush=True)


def report_timing(binary_dir):
    """Print where the build that just ran spent its time, or say why it cannot be told.

    Never fatal: the build succeeded, and a report that could not be produced must not turn that
    into a failure.
    """
    import build_timing

    try:
        entries = build_timing.parse(build_timing.log_path(binary_dir), binary_dir)
    except build_timing.LogError as err:
        print(f"warning: --time could not read the build log: {err}", file=sys.stderr)
        return

    invocations = build_timing.split_invocations(entries)
    if not invocations:
        print("--time: everything was already up to date; ninja recorded no edges.")
        return

    build_timing.report(build_timing.summarise(invocations[-1]), top=15)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", nargs="?", help="Target to build (default: all).")
    parser.add_argument("--preset", help="CMake preset (default: config.json, else "
                                         f"{cfg.DEFAULT_PRESET}).")
    parser.add_argument("--config", help="Build configuration for multi-config generators "
                                         "(e.g. Debug, Release; default: config.json).")
    parser.add_argument("--arch", help=f"vcvars architecture (default: config.json, else {cfg.DEFAULT_ARCH}).")
    configure_group = parser.add_mutually_exclusive_group()
    configure_group.add_argument("--configure", action="store_true",
                                 help="Force a configure and skip the build step.")
    configure_group.add_argument("--no-configure", action="store_true",
                                 help="Never configure, even if the build dir has not been.")
    parser.add_argument("--dry-run", action="store_true", help="Print what would run without executing.")
    parser.add_argument("--time", action="store_true",
                        help="After the build, report where its time went (scripts/build_timing.py).")
    parser.add_argument("--no-jobserver", action="store_true",
                        help="Do not share this machine's job budget with the other checkouts' builds.")
    parser.add_argument("--jobs", type=int,
                        help=f"Size of that shared budget (default: one per core, {jobserver.default_tokens()} here).")
    args = parser.parse_args()

    preset = cfg.preset(args.preset)

    generator = ct.generator_of(preset)
    if generator is None:
        print(f"warning: could not resolve a generator for preset '{preset}'.", file=sys.stderr)

    config = cfg.build_config(args.config, generator)

    # --dry-run stays cheap and side-effect free: it names the environment it would
    # set up rather than running the precommand to build it.
    if args.dry_run:
        env, env_source = None, cfg.precommand() or ("vcvars" if ct.needs_msvc_env(generator) else "inherited")
    else:
        env, env_source = cfg.build_env(generator, args.arch)

    cmake = cfg.find_cmake(env)
    if not cmake:
        print("error: cmake not found in config.json, on PATH, or in the Visual Studio install. "
              "Run `just init`.", file=sys.stderr)
        return 1

    configure_cmd = [cmake, "--preset", preset]
    build_cmd = [cmake, "--build", "--preset", preset]

    # Resolve toolchain programs that may not be on PATH and pin them as absolute
    # paths so the configure works regardless of how the shell is set up:
    #   * Ninja generators need a make program (VS bundles one off-PATH).
    #   * clang presets need clang/clang++ (VS LLVM component preferred, else PATH).
    toolchain = []
    ninja = None
    if generator and "ninja" in generator.lower():
        ninja = cfg.find_ninja(env)
        if ninja:
            configure_cmd += [f"-DCMAKE_MAKE_PROGRAM={ninja}"]
            toolchain.append(f"ninja: {ninja}")
        else:
            print("warning: Ninja generator selected but ninja was not found in config.json, on "
                  "PATH, or in the Visual Studio install.", file=sys.stderr)
    if ct.uses_clang(preset):
        clang = cfg.find_clang(env)
        if clang:
            configure_cmd += [f"-DCMAKE_C_COMPILER={clang['c']}",
                              f"-DCMAKE_CXX_COMPILER={clang['cxx']}"]
            toolchain.append(f"clang: {clang['cxx']}")
        else:
            print("warning: clang preset selected but clang/clang++ were not found. "
                  "Install the \"C++ Clang tools for Windows\" (LLVM) component from the "
                  "Visual Studio Installer, add clang to your PATH, or set tools.clang via "
                  "`just init`.", file=sys.stderr)

    if args.target:
        build_cmd += ["--target", args.target]
    if config:
        build_cmd += ["--config", config]

    binary_dir = ct.binary_dir_of(preset)
    stale, reason = needs_configure(binary_dir)
    configure = args.configure or (stale and not args.no_configure)

    # Only the configure consumes the toolchain file; an already-configured dir has the
    # resolved path in its cache and builds fine without one.
    wants_vcpkg = configure and ct.needs_vcpkg(preset)
    vcpkg_root = cfg.find_vcpkg() if wants_vcpkg else None

    if args.dry_run:
        print(f"preset:    {preset}")
        print(f"generator: {generator}")
        print(f"config:    {config or '(generator default)'}")
        print(f"cmake:     {cmake}")
        print(f"env:       {env_source}")
        if wants_vcpkg:
            print(f"vcpkg:     {vcpkg_root or '(not found -- run `just init`)'}")
        for note in toolchain:
            print(f"toolchain: {note}")
        if configure:
            print("configure: " + " ".join(configure_cmd))
        else:
            print(f"configure: skipped -- {reason}")
        if not args.configure:
            print("build:     " + " ".join(build_cmd))
        return 0

    if wants_vcpkg and not vcpkg_root:
        print(f"error: preset '{preset}' builds against vcpkg, and no vcpkg checkout was found in "
              f"{cfg.rel(cfg.PATH)}, in VCPKG_ROOT, or in the usual install locations.\n"
              f"Run `just init`, which offers to clone and bootstrap one.", file=sys.stderr)
        return 1

    if configure:
        # The File API query must exist before cmake runs, or it writes no codemodel reply and
        # every discovery script (targets, exes, run, idl) comes up empty.
        if binary_dir:
            ct.ensure_query(binary_dir)

        rc = subprocess.run(configure_cmd, env=env).returncode
        if rc:
            print(f"configure failed (exit {rc}).", file=sys.stderr)
            return rc

        write_presets_stamp(binary_dir)

    if args.configure:
        return 0

    repair_deps_log(ninja, binary_dir, env)

    # ninja is a jobserver client, so the cap is a property of the environment it runs in rather
    # than a -j decided here: several checkouts building at once then share one budget instead of
    # each taking the whole machine. See scripts/util/jobserver.py.
    with jobserver.shared_budget(env, enabled=not args.no_jobserver, tokens=args.jobs) as build_env:
        rc = subprocess.run(build_cmd, env=build_env).returncode

    # Read back rather than measured here: ninja's log already holds every edge's duration, and a
    # wall clock around `cmake --build` would count the configure and hide where the time went.
    if args.time and rc == 0:
        report_timing(binary_dir)

    return rc


if __name__ == "__main__":
    sys.exit(main())
