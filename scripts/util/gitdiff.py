"""What counts as our source, and which lines of it a diff touched.

The one implementation of merge-base line ranges and of what is ours to name, format
and cover: two parsers of `@@` hunk headers would drift, so every consumer imports
from here.
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


def is_generated_at(ref, path):
    """Whether `path` carried the generator's banner at `ref`, or on disk when None.

    A diff may name a ref this checkout is not on, and the file sitting beside it
    then belongs to some other branch.
    """
    if ref is None:
        return is_generated(os.path.join(ct.REPO_ROOT, path))
    blob = subprocess.run(["git", "show", f"{ref}:{path}"], cwd=ct.REPO_ROOT,
                          capture_output=True, text=True, errors="replace")
    if blob.returncode != 0:
        return False
    return GENERATED_BANNER.decode() in blob.stdout.split("\n", 1)[0]


def git_output(args):
    result = subprocess.run(["git"] + args, cwd=ct.REPO_ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        return None
    return result.stdout


def git_lines(args):
    out = git_output(args)
    return None if out is None else out.splitlines()


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


# Six kinds of change, ordered as a reviewer weighs them. The split a reader
# actually wants is production against test, but shaders and assets earn their own
# rows: CI compiles a shader and never runs one, and an asset is bytes with no
# line count at all. "Generated" is last because it is normally absent -- a row is
# printed only when it has files.
CATEGORIES = ("Production", "Shaders", "Tests", "Docs", "Build & tooling", "Assets", "Generated")

# Qt Designer forms and Objective-C++ are production source that SOURCE_EXTS, which
# exists for the formatter, has no reason to know about.
EXTRA_SOURCE_EXTS = frozenset([".ui", ".mm"])
SHADER_EXTS = frozenset([".slang", ".slangh"])
DOC_EXTS = frozenset([".md"])


def categorize(path, ref=None):
    """Which of `CATEGORIES` a repo-relative path belongs to, as of `ref`.

    Order is the rule: a test is a test wherever it lives, so `shaders/tests/` is
    Tests and not Shaders, and a generated file is the generator's whatever its
    extension says. Only a source or shader file is probed for the banner, because
    that is all `bgl_idlgen` writes and the probe costs a `git show` each.
    """
    parts = path.split("/")
    ext = os.path.splitext(path)[1].lower()

    if parts[0] == "assets":
        return "Assets"
    if "tests" in parts or os.path.splitext(parts[-1])[0].endswith("_test"):
        return "Tests"
    if ext in (SOURCE_EXTS | SHADER_EXTS) and is_generated_at(ref, path):
        return "Generated"
    if ext in SHADER_EXTS:
        return "Shaders"
    if ext in DOC_EXTS or parts[0] == "docs":
        return "Docs"
    if parts[0] in SOURCE_ROOTS and ext in (SOURCE_EXTS | EXTRA_SOURCE_EXTS):
        return "Production"
    return "Build & tooling"


def numstat(base, tip):
    """[(added, removed, path)] for `base...tip`, renames counted once.

    A binary file reports no line counts, so it arrives as (0, 0, path) and is
    visible only in the file count -- which is the truth about it.
    """
    raw = git_output(["diff", "--numstat", "-M", "-z", f"{base}...{tip}"])
    if raw is None:
        return None

    rows = []
    tokens = raw.split("\0")
    i = 0
    while i < len(tokens):
        record = tokens[i]
        i += 1
        if not record:
            continue
        added, removed, path = record.split("\t", 2)
        # A rename leaves the path field empty and puts old and new in the next
        # two records; the new name is the one that exists to be categorized.
        if not path:
            path = tokens[i + 1]
            i += 2
        rows.append((0 if added == "-" else int(added),
                     0 if removed == "-" else int(removed),
                     path))
    return rows


def breakdown(base, tip):
    """[{category, files, added, removed}] for `base...tip`, in `CATEGORIES` order.

    Empty categories are dropped: the table is read at a glance, and a row of
    zeroes is a line spent saying nothing.
    """
    rows = numstat(base, tip)
    if not rows:
        return []

    # On HEAD the working tree holds the same bytes, and reading them costs no process.
    probe = None if tip == "HEAD" else tip
    totals = {}
    for added, removed, path in rows:
        bucket = totals.setdefault(categorize(path, probe),
                                   {"files": 0, "added": 0, "removed": 0})
        bucket["files"] += 1
        bucket["added"] += added
        bucket["removed"] += removed

    return [dict(category=c, **totals[c]) for c in CATEGORIES if c in totals]
