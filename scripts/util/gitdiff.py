"""What counts as our source, and which lines of it a diff touched.

Shared by tidy.py (naming checks on changed lines) and coverage.py (uncovered lines
of the staged diff, or a ref's): both need the same merge-base line ranges, and two
parsers of `@@` hunk headers would drift.
"""

import os
import subprocess
import sys

import util.cmake_tools as ct

HEADER_EXTS = frozenset([".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp"])
SOURCE_EXTS = HEADER_EXTS | frozenset([".c", ".cc", ".cpp", ".cxx"])

# Where our own code lives. Everything else in the tree -- vcpkg's installed headers,
# _deps checkouts, build output -- is somebody else's to name and to cover.
SOURCE_ROOTS = ("libs", "apps", "examples")

# bgl_idlgen stamps this on every file it writes. A generated file is the generator's
# to name and to format: any edit only lasts until the next build.
GENERATED_BANNER = b"DO NOT EDIT MANUALLY"


def is_generated(path):
    try:
        with open(path, "rb") as fh:
            return GENERATED_BANNER in fh.readline()
    except OSError:
        return False


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
