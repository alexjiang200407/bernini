#!/usr/bin/env python3
"""Build the engine the way a game does: as a subdirectory of somebody else's project.

Usage:
    python scripts/embed.py                  # configure and build tests/embed
    python scripts/embed.py --configure      # configure only, don't compile
    python scripts/embed.py --clean          # wipe the build dir first
    python scripts/embed.py --fresh-deps     # let vcpkg unpack its own tree (~800 MB)
    python scripts/embed.py --dry-run        # print the plan, don't run

tests/embed is the only consumer in the repository. Every other build here is the engine
building itself, where a path built from CMAKE_SOURCE_DIR is right and stays right through
any refactor -- so without this the property is invisible to the whole suite. A configure
proves the build; compiling main.cpp proves the public include surface, which a configure
cannot see. See docs/embedding.md.

The dependencies are the engine's, resolved from the engine's manifest, and by default the
unpacked tree is the configured preset's rather than a second copy of it -- same manifest, same
baseline, so vcpkg finds every package already installed and writes nothing. That tree lives
inside the checkout, so ccache rewrites its include paths relative to each build's own working
directory and they disagree between the two builds; it costs nothing here because an engine
build and a consumer build cannot share an object anyway (docs/embedding.md says why), and the
run-to-run hits this reports are between two consumer builds that both point at it. Do not run
this while a build is running: they would write into that tree at the same time.

Nothing about the flags is guessed to match the engine's own build. The compiler is pinned to
the one config.json records, because ccache keys an object on it; everything else is what a
consumer would actually pass, which is why the hit rate this reports is the one a game gets and
not a flattering one.
"""

import argparse
import os
import shutil
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg

PROJECT_DIR = os.path.join(ct.REPO_ROOT, "tests", "embed")
DEFAULT_BUILD_DIR = os.path.join(ct.REPO_ROOT, "build", "embed")

# The three ccache counters a build moves. A preprocessed hit is still a hit -- it skipped the
# compiler -- so the rate below counts both.
HIT_COUNTERS = ("direct_cache_hit", "preprocessed_cache_hit")
MISS_COUNTER = "cache_miss"


# --- ccache ----------------------------------------------------------------

def ccache_stats(ccache):
    """{counter: int} from `ccache --print-stats`, or None when it cannot be read.

    Read rather than zeroed: the counters are machine-wide and shared with every other build
    on it, so a delta is the only measurement that does not destroy somebody else's.
    """
    if not ccache:
        return None
    try:
        out = subprocess.run([ccache, "--print-stats"], capture_output=True, text=True).stdout
    except OSError:
        return None

    stats = {}
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) == 2 and parts[1].strip().isdigit():
            stats[parts[0].strip()] = int(parts[1])
    return stats or None


def report_cache(before, after):
    """One line on what the compiler was spared, or on why we cannot say."""
    if not before or not after:
        print("Compiler cache: no ccache on PATH, so this build compiled everything.")
        return

    hits = sum(after.get(k, 0) - before.get(k, 0) for k in HIT_COUNTERS)
    misses = after.get(MISS_COUNTER, 0) - before.get(MISS_COUNTER, 0)
    total = hits + misses

    if total <= 0:
        print("Compiler cache: nothing was compiled (the build dir was already up to date).")
        return

    print(f"Compiler cache: {hits}/{total} hits ({100 * hits // total}%), {misses} compiled.")


# --- Configure -------------------------------------------------------------

def shared_vcpkg_tree():
    """The configured preset's unpacked vcpkg tree, when it is there.

    Every consumer reads the engine's manifest and the same triplet, so one unpacked tree serves
    all of them and there is no reason to unpack 800 MB again for one translation unit. Absent,
    vcpkg does the usual thing under the embed build directory.
    """
    build_dir = ct.binary_dir_of(cfg.preset())
    if not build_dir:
        return None
    tree = os.path.join(build_dir, "vcpkg_installed")
    return tree if os.path.isdir(tree) else None


def configure_command(cmake, build_dir, args, env):
    """Ninja, as the scaffolded game's presets use on both hosts, and nothing else assumed."""
    preset = cfg.preset()

    cmd = [cmake, "-S", PROJECT_DIR, "-B", build_dir, "-G", "Ninja",
           f"-DBERNINI_DIR={ct.REPO_ROOT}",
           "-DCMAKE_BUILD_TYPE=Debug",
           "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]

    vcpkg_root = cfg.find_vcpkg()
    if vcpkg_root:
        cmd.append("-DCMAKE_TOOLCHAIN_FILE=" +
                   os.path.join(vcpkg_root, "scripts", "buildsystems", "vcpkg.cmake"))

    ninja = cfg.find_ninja(env)
    if ninja:
        cmd.append(f"-DCMAKE_MAKE_PROGRAM={ninja}")

    # Only where the machine's own preset is a clang one. ccache keys an object on the compiler,
    # so a pin that disagrees with the engine's build is a guaranteed miss -- and where the preset
    # is MSVC there is nothing to share anyway, since ccache declines MSVC outright.
    if ct.uses_clang(preset):
        clang = cfg.find_clang(env)
        if clang:
            cmd += [f"-DCMAKE_C_COMPILER={clang['c']}", f"-DCMAKE_CXX_COMPILER={clang['cxx']}"]

    if not args.fresh_deps:
        shared = shared_vcpkg_tree()
        if shared:
            cmd.append(f"-DVCPKG_INSTALLED_DIR={shared}")
            # The tree is one directory per triplet; without this vcpkg looks under the host
            # default and installs a second copy beside the one we came here to share.
            triplet = ct.cache_var_of(preset, "VCPKG_TARGET_TRIPLET")
            if triplet:
                cmd.append(f"-DVCPKG_TARGET_TRIPLET={triplet}")

    return cmd


# --- Main ------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default=DEFAULT_BUILD_DIR)
    parser.add_argument("--configure", action="store_true",
                        help="configure only; do not compile")
    parser.add_argument("--clean", action="store_true",
                        help="remove the build directory first")
    parser.add_argument("--fresh-deps", action="store_true",
                        help="let vcpkg unpack its own tree instead of sharing the preset's")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    build_dir = args.build_dir if os.path.isabs(args.build_dir) \
        else os.path.join(ct.REPO_ROOT, args.build_dir)

    env, env_description = cfg.build_env("Ninja")

    cmake = cfg.find_cmake(env)
    if not cmake:
        print("error: cmake not found. Run `just init`, or put it on PATH.", file=sys.stderr)
        return 1

    configure = configure_command(cmake, build_dir, args, env)
    build = [cmake, "--build", build_dir, "--target", "bernini_embed"]

    if args.dry_run:
        print(f"environment: {env_description}")
        print(" ".join(configure))
        if not args.configure:
            print(" ".join(build))
        return 0

    if args.clean and os.path.isdir(build_dir):
        shutil.rmtree(build_dir)

    rc = subprocess.run(configure, env=env, cwd=ct.REPO_ROOT).returncode
    if rc != 0:
        print("\nThe engine did not configure as a subdirectory. This is the property "
              "tests/embed exists to catch -- see docs/embedding.md.", file=sys.stderr)
        return rc

    if args.configure:
        print("\nConfigured. Compiling is what proves the include surface: re-run without "
              "--configure.")
        return 0

    ccache = shutil.which("ccache")
    before = ccache_stats(ccache)

    rc = subprocess.run(build, env=env, cwd=ct.REPO_ROOT).returncode
    if rc != 0:
        return rc

    print()
    report_cache(before, ccache_stats(ccache))
    print(f"tests/embed built against {ct.REPO_ROOT}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
