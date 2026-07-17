#!/usr/bin/env python3
"""Format source files in place with clang-format.

Uses tools.clang-format from scripts/config.json (see `just init`) when it is set,
else clang-format on PATH, else the copy bundled with the Visual Studio LLVM
component (located via vswhere, no hardcoded paths).

.slang files are formatted as C# so the dedicated bgl/shaders/.clang-format
(Language: CSharp) applies. clang-format infers a file's language from its
extension, and .slang is unknown -> it would default to C++ and skip the CSharp
config (falling through to the root C++ one). We route .slang through stdin with
--assume-filename=<same path>.cs, which sets both the language (C#) and the
directory used to locate the nearest .clang-format.

Usage:
    python scripts/format.py bgl/src/Foo.cpp bgl/src/Foo.h
    python scripts/format.py --check bgl/src/Foo.cpp   # verify only, no edits
"""

import argparse
import os
import subprocess
import sys

import util.config as cfg


def is_slang(path):
    return os.path.splitext(path)[1].lower() == ".slang"


def slang_assume_filename(path):
    # Keep the directory (so the nearest .clang-format is still bgl/shaders/) but
    # use a .cs extension so clang-format selects the CSharp style document.
    root, _ = os.path.splitext(path)
    return root + ".cs"


def format_slang(clang_format, path, check):
    """Format one .slang file through stdin as C#. Returns a process-style rc."""
    try:
        with open(path, "rb") as f:
            original = f.read()
    except OSError as e:
        print(f"{path}: {e}", file=sys.stderr)
        return 1

    cmd = [clang_format, f"--assume-filename={slang_assume_filename(path)}", "--style=file"]
    proc = subprocess.run(cmd, input=original, capture_output=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode(errors="replace"))
        return proc.returncode

    formatted = proc.stdout
    if formatted == original:
        return 0

    if check:
        print(f"{path}: not formatted (run `just format {path}` to fix)", file=sys.stderr)
        return 1

    with open(path, "wb") as f:
        f.write(formatted)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", help="Files to format in place.")
    parser.add_argument("--check", action="store_true", help="Don't edit; exit non-zero if a file isn't formatted.")
    args = parser.parse_args()

    if not args.files:
        print("usage: format.py <file> [<file> ...]", file=sys.stderr)
        return 1

    clang_format = cfg.find_clang_format()
    if not clang_format:
        if sys.platform == "darwin":
            install = "Install it with `brew install clang-format` (or `brew install llvm`).\n"
        else:
            install = ("Install the \"C++ Clang tools for Windows\" (LLVM) component from the Visual "
                       "Studio Installer,\nor install LLVM and add clang-format to your PATH.\n")
        print("clang-format was not found.\n" + install +
              "If it lives somewhere else, run `just init` to record its path.", file=sys.stderr)
        return 1

    slang_files = [f for f in args.files if is_slang(f)]
    other_files = [f for f in args.files if not is_slang(f)]

    rc = 0

    # .cpp/.h/etc: clang-format's own extension-based language detection finds the
    # correct .clang-format, so format them directly (one invocation).
    if other_files:
        cmd = [clang_format]
        cmd += ["--dry-run", "--Werror"] if args.check else ["-i"]
        cmd += other_files
        if subprocess.run(cmd).returncode != 0:
            rc = 1

    for slang in slang_files:
        if format_slang(clang_format, slang, args.check) != 0:
            rc = 1

    return rc


if __name__ == "__main__":
    sys.exit(main())
