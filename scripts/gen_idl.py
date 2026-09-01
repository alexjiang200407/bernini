#!/usr/bin/env python3
"""Generate the C++ headers and Slang copies from the .slang IDL modules.

Drives the built `bgl_idlgen` tool over every module in `bgl/idl/src`, writing:
  - a banner-stamped Slang copy to   bgl/shaders/src/idl/<rel>.slang   (every module)
  - a generated C++ header, to one of two roots depending on the module:
      * bgl_intfc/include/bgl/<rel>.h  in namespace `bgl`      (IDL_PUBLIC_CPP_SOURCES)
      * <build>/generated/idl/<rel>.h  in namespace `bgl::idl` (IDL_CPP_SOURCES)
    A module in neither list gets no C++ header at all -- correct for the
    interface/generic-only modules, which carry no concrete layout to mirror.

Each module is written under its output root at the SAME path it has relative to
the source root, so its import path, .slang location, #include and .h location
stay in lockstep (see bgl/idl/idlgen.cpp).

**The two lists are read from `bgl/idl/src/CMakelists.txt`, not restated here.**
This must produce byte-for-byte the same tree as the `bgl_idl_generate` CMake
target, and the only way to guarantee that is to route from the same source. A
copy of the lists here drifted once already: it emitted a `bgl::idl::PsoType`
beside the real `bgl::PsoType`, and left the public header stale.

The tool path is resolved from the CMake File API codemodel
(generator-agnostic), like find_executables.py.

Usage:
    just idl                                # regenerate everything
    just idl --config Release               # pick a configuration
    just idl --build                        # build bgl_idlgen first
    just idl --dry-run                      # print commands, don't run
    just idl libs/bgl_extended/idl/src/Vertex.slang  # only these modules
"""

import argparse
import os
import re
import subprocess
import sys

import util.cmake_tools as ct
import util.config as cfg

TOOL = "bgl_idlgen"
SRC_ROOT = os.path.join(ct.REPO_ROOT, "libs", "bgl_extended", "idl", "src")
# Mirrors libs/bgl_extended/idl/src/CMakelists.txt: the private headers are a build artifact, because a
# struct's layout follows the backend it was generated for. Resolved per build dir.
def layout_args(build_dir):
    """--metal-layout when this build dir was configured for Metal: the C++ mirror follows the
    backend's own rules, so the header in a build dir is the layout that build reads."""
    cache = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return []
    with open(cache, encoding="utf-8", errors="replace") as handle:
        metal = any(line.startswith("RENDERER_BACKEND:") and line.rstrip().endswith("METAL")
                    for line in handle)
    return ["--metal-layout"] if metal else []


def cpp_out_dir(tool):
    """<build>/generated/idl, derived from the tool so the headers land beside the binary that
    wrote them -- one build dir per backend, each with its own layout."""
    return os.path.join(os.path.dirname(os.path.dirname(tool)), "generated", "idl")


PUBLIC_CPP_OUT_DIR = os.path.join(ct.REPO_ROOT, "libs", "bgl_intfc", "include", "bgl")
SLANG_OUT_DIR = os.path.join(ct.REPO_ROOT, "libs", "bgl_extended", "shaders", "src", "idl")
IDL_CMAKE = os.path.join(SRC_ROOT, "CMakelists.txt")


def cmake_list(text, name):
    """The .slang entries of a `set(<name> ...)` block in a CMakeLists."""
    match = re.search(r"set\(\s*" + re.escape(name) + r"\b(.*?)\)", text, re.S)
    if not match:
        return set()
    body = re.sub(r"#.*", "", match.group(1))  # strip comments
    return set(re.findall(r"[\w./\\-]+\.slang", body))


def load_routing():
    """(public, internal) module-name sets, read from the IDL CMakeLists."""
    with open(IDL_CMAKE, encoding="utf-8") as handle:
        text = handle.read()

    public = cmake_list(text, "IDL_PUBLIC_CPP_SOURCES")
    internal = cmake_list(text, "IDL_CPP_SOURCES")

    if not public and not internal:
        raise SystemExit(
            f"error: no IDL_CPP_SOURCES / IDL_PUBLIC_CPP_SOURCES found in {IDL_CMAKE}. "
            f"Its format changed; gen_idl.py routes from those lists and cannot guess."
        )

    both = public & internal
    if both:
        raise SystemExit(
            f"error: {', '.join(sorted(both))} appear in BOTH IDL_CPP_SOURCES and "
            f"IDL_PUBLIC_CPP_SOURCES. A module emits one C++ header, in one namespace."
        )

    return public, internal


def cpp_args_for(module, public, internal, tool):
    """The --cpp-out-dir/--namespace flags for one module (empty = Slang copy only)."""
    name = os.path.basename(module)

    if name in public:
        # Committed and shared by every backend, so idlgen refuses a struct that differs on one.
        return ["--cpp-out-dir", PUBLIC_CPP_OUT_DIR, "--namespace", "bgl", "--public"]
    if name in internal:
        return ["--cpp-out-dir", cpp_out_dir(tool)]
    return []


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

    public, internal = load_routing()

    failures = 0
    for module in modules:
        cmd = [
            tool,
            "--src-root", SRC_ROOT,
            "--slang-out-dir", SLANG_OUT_DIR,
            *layout_args(os.path.dirname(os.path.dirname(tool))),
            *cpp_args_for(module, public, internal, tool),
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
