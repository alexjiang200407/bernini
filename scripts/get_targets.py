#!/usr/bin/env python3
"""List all CMake targets via the File API codemodel (generator-agnostic).

Usage:
    python scripts/get_targets.py                 # every target + type
    python scripts/get_targets.py --type EXECUTABLE
    python scripts/get_targets.py --json
    python scripts/get_targets.py --build-dir build/msvc-release

Requires a configured build dir; run `python scripts/build.py` (or any preset
configure) first.
"""

import argparse
import json
import sys

import util.cmake_tools as ct


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", help="Build directory (default: scan build/*).")
    parser.add_argument("--type", help="Filter by target type (e.g. EXECUTABLE, SHARED_LIBRARY, UTILITY).")
    parser.add_argument("--json", action="store_true", help="Emit JSON.")
    args = parser.parse_args()

    build_dirs = ct.find_build_dirs(args.build_dir)
    if not build_dirs:
        print("No CMake File API reply found. Configure a build first "
              "(e.g. python scripts/build.py).", file=sys.stderr)
        return 2

    aggregated = {}
    for build_dir in build_dirs:
        for target in ct.load_targets(build_dir):
            if args.type and target["type"] != args.type.upper():
                continue
            entry = aggregated.setdefault(
                target["name"], {"name": target["name"], "type": target["type"], "configs": set()}
            )
            if target["config"]:
                entry["configs"].add(target["config"])

    rows = sorted(aggregated.values(), key=lambda r: (r["type"], r["name"]))
    if args.json:
        print(json.dumps([{**r, "configs": sorted(r["configs"])} for r in rows], indent=2))
        return 0
    if not rows:
        print("No targets found.", file=sys.stderr)
        return 1

    width = max(len(r["name"]) for r in rows)
    for r in rows:
        print(f"{r['name']:<{width}}  {r['type']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
