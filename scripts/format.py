#!/usr/bin/env python3
"""Format source files in place with clang-format.

Prefers clang-format on PATH; otherwise falls back to the copy bundled with the
Visual Studio LLVM component (located via vswhere, no hardcoded paths).

Usage:
    python scripts/clang_format.py bgl/src/Foo.cpp bgl/src/Foo.h
    python scripts/clang_format.py --check bgl/src/Foo.cpp   # verify only, no edits
"""

import argparse
import subprocess
import sys

import util.cmake_tools as ct


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", help="Files to format in place.")
    parser.add_argument("--check", action="store_true", help="Don't edit; exit non-zero if a file isn't formatted.")
    args = parser.parse_args()

    if not args.files:
        print("usage: clang_format.py <file> [<file> ...]", file=sys.stderr)
        return 1

    clang_format = ct.find_clang_format()
    if not clang_format:
        print("clang-format was not found.\n"
              "Install the \"C++ Clang tools for Windows\" (LLVM) component from the Visual Studio Installer,\n"
              "or install LLVM and add clang-format to your PATH.", file=sys.stderr)
        return 1

    cmd = [clang_format]
    cmd += ["--dry-run", "--Werror"] if args.check else ["-i"]
    cmd += args.files
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
