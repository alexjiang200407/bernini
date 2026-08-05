"""Whether this clone's Git LFS assets are real files or pointer text.

The `filter.lfs.*` entries git needs to smudge an LFS file live in machine-local git
config, which no repository can carry. A clone made before they exist writes 130-byte
pointer text into the working tree in place of every asset, and nothing reports it as a
setup problem: the binaries read a "mesh" that is ASCII and fail on a corrupt file.
`just init` configures the filters; this is what the scripts that launch a binary check
first, so the failure names its own cause.

The scan is filesystem-only -- suffixes come from .gitattributes and the pointer magic is
the first bytes of the file -- because `git lfs ls-files` costs the best part of a second
on every `just run`, and this costs milliseconds.
"""

import os

import util.cmake_tools as ct

# First bytes of a Git LFS pointer file, i.e. what stands in for an asset with no smudge filter.
POINTER_MAGIC = b"version https://git-lfs.github.com/spec/v1"

# Directories the scan never descends into: build output, staged binaries, and any vcpkg
# checkout somebody put inside the tree.
SKIP_DIRS = {".git", "build", "dist", "vcpkg", "vcpkg_installed", "__pycache__"}


def tracked_suffixes():
    """File suffixes .gitattributes routes through the LFS filter, lowercased."""
    suffixes = set()
    try:
        with open(os.path.join(ct.REPO_ROOT, ".gitattributes"), encoding="utf-8") as fh:
            lines = fh.readlines()
    except OSError:
        return suffixes

    for line in lines:
        line = line.strip()
        if line.startswith("#") or "filter=lfs" not in line:
            continue
        pattern = line.split()[0]
        if pattern.startswith("*."):
            suffixes.add(pattern[1:].lower())
    return suffixes


def pointer_files(limit=None):
    """Working-tree files that are LFS pointer text instead of the asset they name."""
    suffixes = tracked_suffixes()
    if not suffixes:
        return []

    found = []
    for root, dirs, files in os.walk(ct.REPO_ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if not name.lower().endswith(tuple(suffixes)):
                continue
            path = os.path.join(root, name)
            try:
                with open(path, "rb") as fh:
                    if fh.read(len(POINTER_MAGIC)) != POINTER_MAGIC:
                        continue
            except OSError:
                continue
            found.append(os.path.relpath(path, ct.REPO_ROOT))
            if limit and len(found) >= limit:
                return found
    return found


def problem():
    """Message describing a broken LFS checkout, or None when the assets are real.

    Stops at the first pointer found: one is as conclusive as a hundred, and the fix is
    the same either way.
    """
    stale = pointer_files(limit=1)
    if not stale:
        return None
    return (
        f"error: the assets in this clone are Git LFS pointers, not files "
        f"(e.g. {stale[0]}).\n"
        f"Binaries reading them fail as though the asset were corrupt. Fix the clone with:\n"
        f"    just init\n"
        f"which installs the filters, points this clone at the object store, and fetches.\n"
        f"If it has already run, the store credentials are the usual cause: see docs/lfs.md."
    )
