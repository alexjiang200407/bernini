#!/usr/bin/env python3
"""Generate the C++ headers and Slang copies from the .slang IDL modules.

Drives the built `bgl_idlgen` tool over every module in `bgl/idl/src`, writing:
  - a banner-stamped Slang copy to   bgl/shaders/src/idl/<rel>.slang  (every module)
  - an internal C++ header to        bgl/src/idl/<rel>.h              (IDL_CPP_SOURCES)
  - a public  C++ header to          bgl/include/bgl/<rel>.h          (IDL_PUBLIC_CPP_SOURCES,
                                                                       in namespace `bgl`)

Each module is written under its output root at the SAME path it has relative to
the source root, so its import path, .slang location, #include and .h location
stay in lockstep (see bgl/idl/idlgen.cpp).

**Which module goes where is decided by bgl/idl/src/CMakelists.txt, and that file
is the single source of truth** -- the two lists are parsed straight out of it
rather than copied here, because a copy drifts. A module in neither list gets no
C++ header at all, only the Slang copy. Passing one output root uniformly (which
this script used to do) silently emitted the public modules into the internal
directory, in the wrong namespace, as files nobody had asked for.

This is the same generation the `bgl_idl_generate` CMake target performs; use it
to regenerate on demand without a full build. The tool path is resolved from the
CMake File API codemodel (generator-agnostic), like find_executables.py.

Usage:
    just idl                                # regenerate everything
    just idl --config Release               # pick a configuration
    just idl --build                        # build bgl_idlgen first
    just idl --dry-run                      # print commands, don't run
    just idl libs/bgl/idl/src/Vertex.slang  # only these modules
"""

import argparse
import os
import re
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg

TOOL = "bgl_idlgen"
SRC_ROOT = os.path.join(ct.REPO_ROOT, "libs", "bgl", "idl", "src")
CPP_OUT_DIR = os.path.join(ct.REPO_ROOT, "libs", "bgl", "src", "idl")
PUBLIC_CPP_OUT_DIR = os.path.join(ct.REPO_ROOT, "libs", "bgl", "include", "bgl")
SLANG_OUT_DIR = os.path.join(ct.REPO_ROOT, "libs", "bgl", "shaders", "src", "idl")
IDL_CMAKE = os.path.join(SRC_ROOT, "CMakelists.txt")


def read_routing():
    """The {module file name -> (cpp out dir, namespace)} routing, read from the CMakeLists.

    Mirrors the IDL_CPP_SOURCES / IDL_PUBLIC_CPP_SOURCES lists there. A module in
    neither list is absent from the mapping and gets no C++ header.
    """
    with open(IDL_CMAKE, encoding="utf-8") as f:
        cmake = f.read()

    def names(var):
        m = re.search(r"set\(\s*" + var + r"\s(.*?)\)", cmake, re.S)
        if not m:
            raise SystemExit(f"error: {var} not found in {IDL_CMAKE}")
        return [n for n in m.group(1).split() if n.endswith(".slang")]

    routing = {n: (CPP_OUT_DIR, None) for n in names("IDL_CPP_SOURCES")}
    routing.update({n: (PUBLIC_CPP_OUT_DIR, "bgl") for n in names("IDL_PUBLIC_CPP_SOURCES")})
    return routing


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
    parser.add_argument("--build-dir", help="Build directory (default: the configured preset's).")
    parser.add_argument("--config", help="Configuration to use (e.g. Debug, Release; default: config.json).")
    parser.add_argument("--build", action="store_true", help="Build bgl_idlgen first.")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without running.")
    args = parser.parse_args()

    config = cfg.artifact_config(args.config)

    if args.build and not args.dry_run:
        build = [sys.executable, os.path.join(ct.REPO_ROOT, "scripts", "build.py"), TOOL]
        if config:
            build += ["--config", config]
        rc = subprocess.run(build).returncode
        if rc:
            return rc

    tool = resolve_tool(cfg.build_dir(args.build_dir), config)
    if not tool:
        print(f"error: no '{TOOL}' executable in the codemodel. Configure a build first "
              f"(`just build {TOOL}`), or pass --build.", file=sys.stderr)
        return 2
    if not os.path.isfile(tool) and not args.dry_run:
        print(f"error: '{tool}' is not built yet. Build it with: `just build {TOOL}` "
              f"(or pass --build).", file=sys.stderr)
        return 1

    # Run from the tool's own directory so its runtime DLLs (slang.dll) resolve,
    # exactly like exec_target.py does.
    cwd = os.path.dirname(tool)

    modules = find_modules(args.modules)
    if not modules:
        print(f"warning: no .slang modules found under {SRC_ROOT}", file=sys.stderr)
        return 0

    routing = read_routing()

    failures = 0
    for module in modules:
        cmd = [
            tool,
            "--src-root", SRC_ROOT,
            "--slang-out-dir", SLANG_OUT_DIR,
            "-I", SRC_ROOT,
        ]

        # A module the CMakeLists routes nowhere gets no --cpp-out-dir, so the tool emits only the
        # Slang copy -- exactly as the build does for it.
        out_dir, namespace = routing.get(os.path.basename(module), (None, None))
        if out_dir:
            cmd += ["--cpp-out-dir", out_dir]
        if namespace:
            cmd += ["--namespace", namespace]

        cmd.append(module)

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
