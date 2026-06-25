#!/usr/bin/env python3
"""Locate built executables via the CMake File API (generator-agnostic).

The File API codemodel is emitted identically for Ninja, Xcode, Unix Makefiles
and Visual Studio / MSBuild, so this resolves the real, per-config executable
paths without parsing CMakeLists.txt or guessing at CMAKE_RUNTIME_OUTPUT_DIRECTORY
(which holds a $<CONFIG> generator expression that only exists at build time).

Usage:
    python scripts/find_executables.py                  # list every executable
    python scripts/find_executables.py --target bgl_tests   # print one path
    python scripts/find_executables.py --json           # machine-readable
    python scripts/find_executables.py --build-dir build/msvc-release
"""

import argparse
import os
import sys

import util.cmake_tools as ct


def collect(build_dir):
    rows = []
    for target in ct.load_targets(build_dir):
        if target["type"] != "EXECUTABLE":
            continue
        for path in target["artifacts"]:
            rows.append(
                {
                    "target": target["name"],
                    "config": target["config"],
                    "path": path,
                    "exists": os.path.isfile(path),
                }
            )
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", help="Build directory (default: scan build/*).")
    parser.add_argument("--target", help="Print the path of a single target and exit.")
    parser.add_argument("--config", help="Filter to one configuration (e.g. Debug, Release).")
    parser.add_argument("--json", action="store_true", help="Emit JSON.")
    args = parser.parse_args()

    build_dirs = ct.find_build_dirs(args.build_dir)
    if not build_dirs:
        print("No CMake File API reply found. Configure a build first "
              "(e.g. python scripts/build.py).", file=sys.stderr)
        return 2

    rows = []
    for build_dir in build_dirs:
        ct.ensure_query(build_dir)
        rows.extend(collect(build_dir))

    if args.config:
        rows = [r for r in rows if r["config"].lower() == args.config.lower()]

    if args.target:
        matches = [r for r in rows if r["target"] == args.target]
        matches.sort(key=lambda r: not r["exists"])  # prefer one that exists
        if not matches:
            print(f"No executable target named '{args.target}'.", file=sys.stderr)
            return 1
        print(matches[0]["path"])
        return 0

    if args.json:
        import json
        print(json.dumps(rows, indent=2))
        return 0

    if not rows:
        print("No executable targets found in the codemodel.", file=sys.stderr)
        return 1

    width = max(len(r["target"]) for r in rows)
    for r in rows:
        flag = "" if r["exists"] else "  (not built)"
        print(f"{r['target']:<{width}}  {r['config']:<8}  {r['path']}{flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
