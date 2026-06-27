#!/usr/bin/env python3
"""Generate the C++ headers and Slang copies from the .slang IDL modules.

Drives the built `bgl_idlgen` tool over every module in `bgl/idl/src`, writing:
  - a banner-stamped Slang copy to   bgl/shaders/idl/<rel>.slang   (every module)
  - a generated C++ header to        bgl/src/idl/<rel>.h           (modules that
                                                                    define structs)

Each module is written under both output roots at the SAME path it has relative
to the source root, so its import path, .slang location, #include and .h location
stay in lockstep (see bgl/idl/idlgen.cpp). Interface/generic-only modules carry
no concrete layout, so the tool skips their C++ header on its own -- which is why
this passes both output roots uniformly and needs no per-file list.

This is the same generation the `bgl_idl_generate` CMake target performs; use it
to regenerate on demand without a full build. The tool path is resolved from the
CMake File API codemodel (generator-agnostic), like find_executables.py.

Usage:
    python scripts/gen_idl.py                       # regenerate everything
    python scripts/gen_idl.py --config Release      # pick a configuration
    python scripts/gen_idl.py --build               # build bgl_idlgen first
    python scripts/gen_idl.py --dry-run             # print commands, don't run
    python scripts/gen_idl.py bgl/idl/src/Vertex.slang   # only these modules
"""

import argparse
import os
import subprocess
import sys

import util.cmake_tools as ct

TOOL = "bgl_idlgen"
SRC_ROOT = os.path.join(ct.REPO_ROOT, "bgl", "idl", "src")
CPP_OUT_DIR = os.path.join(ct.REPO_ROOT, "bgl", "src", "idl")
SLANG_OUT_DIR = os.path.join(ct.REPO_ROOT, "bgl", "shaders", "src", "idl")


def resolve_tool(build_dir, config):
    """Absolute path to the built bgl_idlgen, via the File API (or None)."""
    candidates = []
    for bd in ct.find_build_dirs(build_dir):
        ct.ensure_query(bd)
        for target in ct.load_targets(bd):
            if target["name"] != TOOL or target["type"] != "EXECUTABLE":
                continue
            if config and target["config"].lower() != config.lower():
                continue
            candidates.extend(target["artifacts"])
    # Prefer an artifact that actually exists on disk.
    candidates.sort(key=lambda p: not os.path.isfile(p))
    return candidates[0] if candidates else None


def find_modules(paths):
    """Resolve the .slang modules to generate (default: all under SRC_ROOT)."""
    if paths:
        return [os.path.abspath(p) for p in paths]
    modules = []
    for root, _dirs, files in os.walk(SRC_ROOT):
        for name in files:
            if name.endswith(".slang"):
                modules.append(os.path.join(root, name))
    return sorted(modules)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("modules", nargs="*", help="Specific .slang modules (default: all).")
    parser.add_argument("--build-dir", help="Build directory (default: scan build/*).")
    parser.add_argument("--config", help="Configuration to use (e.g. Debug, Release).")
    parser.add_argument("--build", action="store_true", help="Build bgl_idlgen first.")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without running.")
    args = parser.parse_args()

    if args.build and not args.dry_run:
        build = [sys.executable, os.path.join(ct.REPO_ROOT, "scripts", "build.py"), TOOL]
        if args.config:
            build += ["--config", args.config]
        rc = subprocess.run(build).returncode
        if rc:
            return rc

    tool = resolve_tool(args.build_dir, args.config)
    if not tool:
        print(f"error: no '{TOOL}' executable in the codemodel. Configure a build first "
              f"(python scripts/build.py {TOOL}), or pass --build.", file=sys.stderr)
        return 2
    if not os.path.isfile(tool) and not args.dry_run:
        print(f"error: '{tool}' is not built yet. Build it with: "
              f"python scripts/build.py {TOOL}  (or pass --build).", file=sys.stderr)
        return 1

    # Run from the tool's own directory so its runtime DLLs (slang.dll) resolve,
    # exactly like exec_target.py does.
    cwd = os.path.dirname(tool)

    modules = find_modules(args.modules)
    if not modules:
        print(f"warning: no .slang modules found under {SRC_ROOT}", file=sys.stderr)
        return 0

    failures = 0
    for module in modules:
        cmd = [
            tool,
            "--src-root", SRC_ROOT,
            "--slang-out-dir", SLANG_OUT_DIR,
            "--cpp-out-dir", CPP_OUT_DIR,
            "-I", SRC_ROOT,
            module,
        ]
        rel = os.path.relpath(module, SRC_ROOT)
        if args.dry_run:
            print(f"[{rel}] " + " ".join(cmd))
            continue
        rc = subprocess.run(cmd, cwd=cwd).returncode
        status = "ok" if rc == 0 else f"FAILED (exit {rc})"
        print(f"[{status}] {rel}")
        failures += rc != 0

    if failures:
        print(f"{failures} module(s) failed to generate.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
