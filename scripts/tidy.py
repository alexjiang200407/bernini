#!/usr/bin/env python3
"""Check identifier naming with clang-tidy, against the .clang-tidy files in the tree.

The rules are STYLE.md's, encoded in the root .clang-tidy and narrowed by the ones
below it; docs/naming.md explains the split. This script only drives clang-tidy --
what counts as a good name lives in the config, not here.

Every file is checked as its own translation unit, headers included. That costs a
reparse per header, and buys two things worth more: header-only code (most of
core/containers) is checked at all, and each file is judged by the .clang-tidy
nearest *it*. clang-tidy reads a check's options once per TU, from the config
beside the main file -- so a header pulled into another subsystem's TU would
otherwise be judged by that subsystem's rules, and core's snake_case containers
would fail against bgl's PascalCase. HeaderFilterRegex stays empty everywhere for
the same reason: a file is diagnosed when it is the main file, and only then.

The database is rewritten before use, into <build>/clang-tidy/, for two reasons.

Headers have no entry of their own, so one is synthesized from the nearest .cpp in
the same subsystem. Letting clang-tidy interpolate instead reaches for whatever
shares the longest path prefix, which crosses targets and produces pages of parse
errors; deciding here means a header is only checked when something that compiles
sits beside it.

And a binary PCH is readable only by the clang that wrote it, which clang-tidy
almost never is -- Apple ships none, so on macOS it comes from Homebrew's LLVM
while the build uses Xcode's. Dropping -include-pch leaves the textual -include of
the same header, which parses under any clang. Slower, and the only portable
answer: the project's sources deliberately don't include the standard library, so
without that force-include nothing here parses at all.

Needs a build directory holding compile_commands.json, so a Ninja or Makefile
generator: the Visual Studio generator does not write one. `just build --preset
windows-clang-dx12-debug` produces one on Windows.

Usage:
    just tidy                              # every source file the preset compiles
    just tidy libs/core                    # only this subtree
    just tidy libs/bgl/src/scene/Scene.cpp
    just tidy --fix                        # apply the renames clang-tidy suggests
    just tidy --changed                    # staged files, staged lines only (the hook)
    just tidy --changed origin/master      # what this branch changed, its lines only
"""

import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import util.cmake_tools as ct
import util.config as cfg

HEADER_EXTS = frozenset([".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp"])
SOURCE_EXTS = HEADER_EXTS | frozenset([".c", ".cc", ".cpp", ".cxx"])

# Where our own code lives. Everything else in the tree -- vcpkg's installed headers,
# _deps checkouts, build output -- is somebody else's to name.
SOURCE_ROOTS = ("libs", "apps", "examples")

# bgl_idlgen stamps this on every file it writes; renaming a generated identifier
# only lasts until the next build.
GENERATED_BANNER = b"DO NOT EDIT MANUALLY"


def is_generated(path):
    try:
        with open(path, "rb") as fh:
            return GENERATED_BANNER in fh.readline()
    except OSError:
        return False


# --- What to check ---------------------------------------------------------

def walk_sources(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "_deps")]
        for name in filenames:
            if os.path.splitext(name)[1].lower() in SOURCE_EXTS:
                yield os.path.join(dirpath, name)


def collect(paths):
    """Every source file under `paths` (default: the source roots), sorted."""
    roots = [os.path.abspath(p) for p in paths] or [
        os.path.join(ct.REPO_ROOT, r) for r in SOURCE_ROOTS
    ]
    found = set()
    for root in roots:
        if os.path.isfile(root):
            found.add(root)
        else:
            found.update(walk_sources(root))
    return sorted(f for f in found if not is_generated(f))


def git_lines(args):
    result = subprocess.run(["git"] + args, cwd=ct.REPO_ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        return None
    return result.stdout.splitlines()


def changed(ref):
    """{abs path: [(first, last), ...]} for files `ref` changed, or the staged diff.

    Only the touched lines, because every existing violation in a file would
    otherwise land on whoever next edits it.
    """
    diff = ["diff", "--unified=0", "--diff-filter=ACMR"]
    diff += [f"{ref}...HEAD"] if ref else ["--cached"]
    lines = git_lines(diff + ["--"] + [f"*{ext}" for ext in sorted(SOURCE_EXTS)])
    if lines is None:
        return None

    ranges = {}
    current = None
    for line in lines:
        if line.startswith("+++ b/"):
            path = os.path.join(ct.REPO_ROOT, line[6:])
            current = ranges.setdefault(os.path.abspath(path), [])
        elif line.startswith("@@") and current is not None:
            # @@ -old,n +new,n @@ -- the new-side span is the one that exists now.
            span = line.split("+", 1)[1].split(" ", 1)[0]
            start, _, count = span.partition(",")
            count = int(count) if count else 1
            if count:
                current.append((int(start), int(start) + count - 1))
    return {p: r for p, r in ranges.items() if r and not is_generated(p)}


# --- Running clang-tidy ----------------------------------------------------

def compile_db_dir(build_dir):
    """A build directory holding compile_commands.json, or None."""
    candidates = [build_dir] if build_dir else ct.find_build_dirs(cfg.build_dir())
    for candidate in candidates:
        if candidate and os.path.isfile(os.path.join(candidate, "compile_commands.json")):
            return candidate
    return None


# A quoted path or a bare run of non-space, so a PCH under "Program Files" is
# consumed whole rather than leaving half of it behind as a stray argument.
_ARG = r'("[^"]*"|\S+)'

PCH_FLAGS = [
    re.compile(r"-Xclang\s+-include-pch\s+-Xclang\s+" + _ARG),
    re.compile(r"-include-pch[\s=]+" + _ARG),
    re.compile(r"-Winvalid-pch"),
    # MSVC's equivalents; /Yc also means this TU exists only to build the PCH.
    re.compile(r"/(Yu|Yc|Fp)" + _ARG + "?"),
]

# What CMake puts at the end of a compile line: where the object goes and which
# source produced it. Both have to come off before the line can name a header.
OBJECT_AND_SOURCE = [
    re.compile(r"\s-o\s+" + _ARG),
    re.compile(r"\s/(Fo|Fd)" + _ARG),
    re.compile(r"\s-c\s+" + _ARG),
]


def subsystem_of(path):
    """Nearest ancestor holding a CMakeLists.txt -- the target that would compile this."""
    directory = os.path.dirname(os.path.abspath(path))
    while directory.startswith(ct.REPO_ROOT):
        if os.path.isfile(os.path.join(directory, "CMakeLists.txt")):
            return directory
        parent = os.path.dirname(directory)
        if parent == directory:
            break
        directory = parent
    return None


def header_entry(entry, header):
    """A database entry that compiles `header` with the flags `entry` uses."""
    command = entry["command"]
    for pattern in OBJECT_AND_SOURCE:
        command = pattern.sub("", command)
    # Compiling a header as the main file means saying so: otherwise clang-cl parses
    # it as C, and clang objects to the `#pragma once` it is about to read.
    language = "/TP" if re.match(r'\S*cl(\.exe)?"?\s', command) else "-x c++-header"
    return dict(entry, file=header, command=f'{command} {language} "{header}"', output=None)


def match_entries(files, entries):
    """Pair each file with the database entry whose flags should compile it.

    A .cpp is only ever its own entry: if the configured preset doesn't build it --
    the D3D12 backend on macOS, apps/editor without Qt -- there is nothing to say
    about it, and guessing flags produces a page of parse errors instead of a
    finding. A header has no entry of its own, so it borrows from the longest-prefix
    match within its own subsystem, which is the .cpp most likely to include it.

    A header only qualifies if its own directory holds a compiled source, or if it
    is part of a subsystem's public `include/` tree. That is what keeps the
    platform-specific corners out: nothing in src/win32/ compiles on macOS, so
    nothing there is judged against a toolchain that could never parse it.
    """
    by_file = {os.path.normpath(e["file"]): e for e in entries}
    compiled_dirs = {os.path.dirname(os.path.normpath(e["file"])) for e in entries}
    within = {}
    for entry in entries:
        root = subsystem_of(entry["file"])
        if root:
            within.setdefault(root, []).append(entry)

    matched, skipped = [], []
    for path in files:
        entry = by_file.get(os.path.normpath(path))
        if entry:
            matched.append((path, entry))
            continue

        root = subsystem_of(path)
        public = root and os.path.abspath(path).startswith(os.path.join(root, "include") + os.sep)
        if os.path.splitext(path)[1].lower() not in HEADER_EXTS:
            skipped.append(path)
            continue
        if not public and os.path.dirname(os.path.abspath(path)) not in compiled_dirs:
            skipped.append(path)
            continue

        candidates = within.get(root or "", [])
        best = max(candidates, key=lambda e: _shared_prefix(e["file"], path), default=None)
        if best:
            matched.append((path, header_entry(best, path)))
        else:
            skipped.append(path)
    return matched, skipped


def _shared_prefix(a, b):
    return len(os.path.commonpath([os.path.abspath(a), os.path.abspath(b)]))


def strip_pch_arguments(arguments):
    """`-Xclang -include-pch -Xclang <path>` and friends removed, value included."""
    kept = []
    skip = 0
    for i, argument in enumerate(arguments):
        if skip:
            skip -= 1
            continue
        if argument == "-include-pch":
            # -Xclang wraps each of the pair, so the -Xclang before it goes too.
            if kept and kept[-1] == "-Xclang":
                kept.pop()
            skip = 2 if arguments[i + 1 : i + 2] == ["-Xclang"] else 1
            continue
        if argument == "-Winvalid-pch" or argument.startswith(("/Yu", "/Yc", "/Fp")):
            continue
        kept.append(argument)
    return kept


def sanitized_db(build_dir, files):
    """Write a database that compiles exactly `files`, PCH-free.

    Every entry keeps its textual `-include cmake_pch.hxx`, so the standard library
    is still in scope for sources that (by project rule) never include it. Returns
    (directory, checkable files, files nothing in the database can compile).
    """
    with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as fh:
        entries = json.load(fh)

    usable = []
    for entry in entries:
        # The TU that exists only to produce the PCH has nothing of ours to check.
        if os.path.basename(entry.get("file", "")).startswith("cmake_pch."):
            continue
        command = entry.get("command")
        if command:
            for pattern in PCH_FLAGS:
                command = pattern.sub("", command)
            entry = dict(entry, command=command)
        elif "arguments" in entry:
            entry = dict(entry, arguments=strip_pch_arguments(entry["arguments"]))
        usable.append(entry)

    matched, skipped = match_entries(files, usable)

    directory = os.path.join(build_dir, "clang-tidy")
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "compile_commands.json"), "w", encoding="utf-8") as fh:
        json.dump([entry for _, entry in matched], fh, indent=1)
    return directory, [path for path, _ in matched], skipped


def line_filter(path, spans):
    return json.dumps([{"name": os.path.basename(path), "lines": [[a, b] for a, b in spans]}])


def check(tidy, build_dir, path, args, spans=None):
    """Run clang-tidy over one file. Returns (path, returncode, output)."""
    # Only naming gates the exit code. A parse diagnostic still shows, and still fails
    # the file, but a stray compiler warning in someone's header does not.
    cmd = [tidy, "-p", build_dir, "--quiet", "--warnings-as-errors=readability-identifier-naming"]
    if args.fix:
        cmd.append("--fix")
    if args.checks:
        cmd.append(f"--checks={args.checks}")
    if spans:
        cmd.append(f"--line-filter={line_filter(path, spans)}")
    cmd.append(path)

    result = subprocess.run(cmd, capture_output=True, text=True, cwd=ct.REPO_ROOT)
    return path, result.returncode, (result.stdout or "") + (result.stderr or "")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("paths", nargs="*", help="Files or directories to check (default: all).")
    parser.add_argument("--fix", action="store_true", help="Apply clang-tidy's fixes in place.")
    parser.add_argument(
        "--changed", nargs="?", const="", metavar="REF",
        help="Only lines changed against REF, or staged but uncommitted when REF is omitted.",
    )
    parser.add_argument("--build-dir", help="Build directory holding compile_commands.json.")
    parser.add_argument("--preset", help="Take the build directory from this preset instead.")
    parser.add_argument("--checks", help="Override the Checks from .clang-tidy.")
    parser.add_argument("--jobs", type=int, default=os.cpu_count(), help="Files to check at once.")
    parser.add_argument(
        "--if-configured", action="store_true",
        help="Exit 0 rather than failing when clang-tidy or a compile database is missing.",
    )
    args = parser.parse_args()

    tidy = cfg.find_clang_tidy()
    if not tidy:
        if args.if_configured:
            return 0
        if sys.platform == "darwin":
            install = "Install it with `brew install llvm`."
        elif sys.platform == "win32":
            install = ('Install the "C++ Clang tools for Windows" (LLVM) component from the '
                       "Visual Studio Installer, or LLVM itself.")
        else:
            install = "Install it with your package manager, e.g. `apt install clang-tidy`."
        print(f"clang-tidy was not found.\n{install}\n"
              "If it lives somewhere else, run `just init` to record its path.", file=sys.stderr)
        return 1

    build_dir = args.build_dir or (ct.binary_dir_of(args.preset) if args.preset else None)
    build_dir = compile_db_dir(build_dir)
    if not build_dir:
        if args.if_configured:
            return 0
        print("No compile_commands.json in any build directory.\n"
              "clang-tidy needs one, and only the Ninja and Makefile generators write it -- the "
              "Visual Studio\ngenerator does not. Configure a Ninja preset first, e.g.\n"
              "    just build --preset windows-clang-dx12-debug", file=sys.stderr)
        return 1

    if args.changed is not None:
        spans = changed(args.changed or None)
        if spans is None:
            return 1
    else:
        spans = {path: None for path in collect(args.paths)}

    db_dir, files, skipped = sanitized_db(build_dir, sorted(spans))
    if skipped:
        # Never silently: "0 findings" has to mean checked, not passed over.
        print(f"{len(skipped)} file(s) skipped -- the configured preset does not compile them "
              f"(e.g. {cfg.rel(skipped[0])}).", file=sys.stderr)
    if not files:
        return 0

    failures = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(check, tidy, db_dir, path, args, spans[path]) for path in files]
        for future in futures:
            path, rc, output = future.result()
            if output.strip():
                sys.stdout.write(output if output.endswith("\n") else output + "\n")
            if rc != 0:
                failures += 1

    print(f"\nclang-tidy: {len(files)} file(s) checked, {failures} with findings.", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
