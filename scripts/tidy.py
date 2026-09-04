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
    just tidy libs/bgl_extended/src/scene/Scene.cpp
    just tidy --fix                        # apply the renames clang-tidy suggests
    just tidy --changed                    # staged files, staged lines only (the hook)
    just tidy --changed origin/master      # what this branch changed, its lines only
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import util.cmake_tools as ct
import util.config as cfg
import util.include_style as include_style
from util.gitdiff import HEADER_EXTS, SOURCE_EXTS, SOURCE_ROOTS, changed, is_generated


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


# --- Running clang-tidy ----------------------------------------------------

def compile_db_dir(build_dir, wanted=()):
    """A build directory holding compile_commands.json, or None.

    Several build dirs can have one -- a Windows DX12 and a backend-less macOS preset, say --
    and they do not compile the same files. Prefer a database that actually covers `wanted`,
    because clang-tidy given a file its database has never heard of does not fail: it
    falls back to no flags at all, and then reports the whole translation unit as
    incompatible with C++98 rather than saying it had no compile command.
    """
    candidates = [build_dir] if build_dir else ct.find_build_dirs(cfg.build_dir())
    candidates = [c for c in candidates
                  if c and os.path.isfile(os.path.join(c, "compile_commands.json"))]
    if not candidates or not wanted:
        return candidates[0] if candidates else None

    targets = {os.path.normcase(os.path.abspath(p)) for p in wanted}

    best, best_hits = candidates[0], -1
    for candidate in candidates:
        try:
            with open(os.path.join(candidate, "compile_commands.json"), encoding="utf-8") as fh:
                entries = json.load(fh)
        except (OSError, ValueError):
            continue

        covered = {os.path.normcase(os.path.abspath(os.path.join(e.get("directory", ""),
                                                                e.get("file", ""))))
                   for e in entries}
        hits = len(targets & covered)
        if hits > best_hits:
            best, best_hits = candidate, hits

    return best


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


def macos_sysroot():
    """`-isysroot <sdk>` for macOS, or "" everywhere else.

    Apple's clang finds the SDK implicitly, so CMake's commands carry no -isysroot
    and Homebrew's clang-tidy then cannot find <algorithm>. It does not fail
    cleanly: the parse collapses and the naming check reports whatever survived, so
    a free function comes back as a global variable wanting a g_ prefix. Passing
    the SDK explicitly is what keeps that from depending on who exported SDKROOT --
    Apple's /usr/bin/python3 does, a Homebrew one does not.
    """
    if sys.platform != "darwin":
        return ""
    sdk = os.environ.get("SDKROOT")
    if not sdk or not os.path.isdir(sdk):
        try:
            sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True,
                                 text=True, check=True).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            return ""
    return f' -isysroot "{sdk}"' if sdk else ""


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
    #
    # Only when the borrowed command does not already say it. Repeating /TP is
    # diagnosed as overriding-option, which /WX -- which this project builds with --
    # turns into an error that aborts the parse, and the failure then presents as the
    # whole translation unit being incompatible with C++98 rather than as a duplicate flag.
    msvc     = re.match(r'\S*cl(\.exe)?"?\s', command) is not None
    language = "/TP" if msvc else "-x c++-header"
    if re.search(r"(^|\s)[/-]TP(\s|$)", command) or "-x c++-header" in command:
        language = ""

    return dict(
        entry,
        file=header,
        command=re.sub(r"\s+", " ", f'{command} {language} "{header}"'),
        output=None)


def silence_warnings(command):
    """The build's warning policy removed, so only real errors can abort a parse.

    clang-cl reads /Wall as -Weverything, and this project also builds with /WX. Under
    clang-tidy that combination makes pedantic diagnostics fatal -- c++98-compat fires on
    any modern header, overriding-option on a duplicated /TP -- and the first one aborts
    the translation unit, burying the naming findings that are the whole point.

    It stayed hidden because --changed filters findings to the lines a diff touched, and a
    small edit to an existing file filters them all out. A newly added file has no
    unchanged lines to hide behind, so it was the first to fail.

    Reproducing the build's warning policy buys nothing here: the compiler enforces it
    during the build. Genuine errors still stop us; only warnings go quiet.
    """
    command = re.sub(r"(^|\s)[/-](WX|Wall)(\s|$)", r"\1\3", command)
    return f"{command} -Wno-everything"


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

    matched, skipped, borrowed = [], [], {}
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
            borrowed[path] = best
        else:
            skipped.append(path)
    return matched, skipped, borrowed


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


# CMake writes the force-include either bare or wrapped, one -Xclang per word.
PCH_INCLUDE = re.compile(r'(-include(?:\s+-Xclang)?[\s=]+)("?)([^\s"]*cmake_pch\.[^\s"]*)\2')

# What CMake writes at the top of its generated PCH header. It makes everything the
# PCH pulls in a system header, and clang-tidy has nothing to say about those -- so a
# header the PCH includes could never be diagnosed through a source that includes it.
SYSTEM_HEADER_PRAGMA = re.compile(r"^\s*#pragma\s+clang\s+system_header\s*$", re.MULTILINE)


def unsystem_pch(command, directory):
    """`command` with its force-included PCH swapped for a non-system copy.

    The copy holds absolute includes, so it parses the same from anywhere.
    """
    def swap(match):
        source = match.group(3)
        digest = hashlib.sha1(source.encode("utf-8")).hexdigest()[:12]
        target = os.path.join(directory, f"{digest}_{os.path.basename(source)}")
        try:
            with open(source, encoding="utf-8") as fh:
                text = fh.read()
        except OSError:
            return match.group(0)
        with open(target, "w", encoding="utf-8") as fh:
            fh.write(SYSTEM_HEADER_PRAGMA.sub("", text))
        return f'{match.group(1)}{match.group(2)}{target}{match.group(2)}'

    return PCH_INCLUDE.sub(swap, command)


def sanitized_db(build_dir, files):
    """Write a database that compiles exactly `files`, PCH-free.

    Every entry keeps its textual `-include cmake_pch.hxx`, so the standard library
    is still in scope for sources that (by project rule) never include it. Returns
    (directory, checkable files, files nothing in the database can compile, and the
    source each header borrowed its flags from).
    """
    with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as fh:
        entries = json.load(fh)

    sysroot = macos_sysroot()
    directory = os.path.join(build_dir, "clang-tidy")
    os.makedirs(directory, exist_ok=True)

    usable = []
    for entry in entries:
        # The TU that exists only to produce the PCH has nothing of ours to check.
        if os.path.basename(entry.get("file", "")).startswith("cmake_pch."):
            continue
        command = entry.get("command")
        if command:
            for pattern in PCH_FLAGS:
                command = pattern.sub("", command)
            if sysroot and "-isysroot" not in command:
                command += sysroot
            entry = dict(entry, command=unsystem_pch(silence_warnings(command), directory))
        elif "arguments" in entry:
            entry = dict(entry, arguments=strip_pch_arguments(entry["arguments"]))
        usable.append(entry)

    matched, skipped, borrowed = match_entries(files, usable)

    # The lender's own entry goes in too: a header the PCH already includes is checked
    # through it instead of as its own translation unit. See `check`. One command per
    # file -- a source compiled into two targets has two, and clang-tidy would then
    # parse it once per command.
    written = [entry for _, entry in matched]
    seen = {os.path.normpath(entry["file"]) for entry in written}
    for entry in borrowed.values():
        if os.path.normpath(entry["file"]) not in seen:
            seen.add(os.path.normpath(entry["file"]))
            written.append(entry)

    with open(os.path.join(directory, "compile_commands.json"), "w", encoding="utf-8") as fh:
        json.dump(written, fh, indent=1)
    lenders = {path: entry["file"] for path, entry in borrowed.items()}
    return directory, [path for path, _ in matched], skipped, lenders


def line_filter(path, spans):
    entry = {"name": os.path.basename(path)}
    if spans:
        entry["lines"] = [[a, b] for a, b in spans]
    return json.dumps([entry])


# A header the translation unit's PCH already includes, parsed a second time as the
# main file. `#pragma once` does not stop it: the guard only suppresses an #include,
# and the main file is not one.
PRE_INCLUDED = "included multiple times, additional include site here"


def run_tidy(tidy, build_dir, main_file, args, filters=()):
    # Only our own checks gate the exit code. A parse diagnostic still shows, and still fails
    # the file, but a stray compiler warning in someone's header does not. include-cleaner is
    # named here for every subsystem, and runs only in the ones whose .clang-tidy enables it.
    cmd = [tidy, "-p", build_dir, "--quiet",
           "--warnings-as-errors=readability-identifier-naming,misc-include-cleaner"]
    if args.fix:
        cmd.append("--fix")
    if args.checks:
        cmd.append(f"--checks={args.checks}")
    cmd += list(filters)
    cmd.append(main_file)

    result = subprocess.run(cmd, capture_output=True, text=True, cwd=ct.REPO_ROOT)
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def check(tidy, build_dir, path, args, spans=None, lender=None):
    """Run clang-tidy over one file. Returns (path, returncode, output).

    A header its own PCH pre-includes cannot be the main file -- every declaration in
    it is a redefinition, and the parse dies before a name is looked at. Such a header
    is checked through the source it borrowed its flags from instead, filtered back
    down to itself, which is the translation unit it is really compiled in anyway.
    """
    filters = [f"--line-filter={line_filter(path, spans)}"] if spans else []
    rc, output = run_tidy(tidy, build_dir, path, args, filters)

    if rc != 0 and lender and PRE_INCLUDED in output:
        filters = [f"--line-filter={line_filter(path, spans)}",
                   f"--header-filter={re.escape(os.path.abspath(path))}"]
        rc, output = run_tidy(tidy, build_dir, lender, args, filters)

    return path, rc, output


def restyle_includes(db_dir, files):
    """clang-tidy's insertions given the bracket STYLE.md asks for. Returns how many moved.

    `misc-include-cleaner` writes every insertion quoted except the standard library's, so a
    --fix run leaves `"bgl/IScene.h"` where the rule wants `<bgl/IScene.h>`. Nothing about
    the check can be configured to do it, so it is done afterwards, from the same database:
    see util/include_style.
    """
    with open(os.path.join(db_dir, "compile_commands.json"), encoding="utf-8") as fh:
        entries = {os.path.normpath(entry["file"]): entry for entry in json.load(fh)}

    moved = 0
    for path in files:
        entry = entries.get(os.path.normpath(os.path.abspath(path)))
        if entry:
            moved += len(include_style.restyle_file(path, entry, ct.REPO_ROOT))
    return moved


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

    # The file list is worked out first, so the database can be chosen by which one covers it.
    if args.changed is not None:
        spans = changed(args.changed or None)
        if spans is None:
            return 1
    else:
        spans = {path: None for path in collect(args.paths)}

    build_dir = args.build_dir or (ct.binary_dir_of(args.preset) if args.preset else None)
    build_dir = compile_db_dir(build_dir, sorted(spans))
    if not build_dir:
        if args.if_configured:
            return 0
        print("No compile_commands.json in any build directory.\n"
              "clang-tidy needs one, and only the Ninja and Makefile generators write it -- the "
              "Visual Studio\ngenerator does not. Configure a Ninja preset first, e.g.\n"
              "    just build --preset windows-clang-dx12-debug", file=sys.stderr)
        return 1

    db_dir, files, skipped, lenders = sanitized_db(build_dir, sorted(spans))
    if skipped:
        # Never silently: "0 findings" has to mean checked, not passed over.
        print(f"{len(skipped)} file(s) skipped -- the configured preset does not compile them "
              f"(e.g. {cfg.rel(skipped[0])}).", file=sys.stderr)
    if not files:
        return 0

    failures = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(check, tidy, db_dir, path, args, spans[path], lenders.get(path))
                   for path in files]
        for future in futures:
            path, rc, output = future.result()
            if output.strip():
                sys.stdout.write(output if output.endswith("\n") else output + "\n")
            if rc != 0:
                failures += 1

    if args.fix:
        moved = restyle_includes(db_dir, files)
        if moved:
            print(f"include style: {moved} include(s) rebracketed. Run `just format` -- the "
                  "bracket decides the sort order.", file=sys.stderr)

    print(f"\nclang-tidy: {len(files)} file(s) checked, {failures} with findings.", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
