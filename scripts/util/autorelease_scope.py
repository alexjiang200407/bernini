"""Every Metal entry point in bgpu drains what it autoreleases, or says why it does not.

A Metal object autoreleased with no pool on the thread leaks, and bgpu's callers hold none: a
compute client beside the renderer has no net to catch it (libs/bgpu/CLAUDE.md, Metal). So each
out-of-line member function under libs/bgpu/src/metal either pushes a pool -- ScopeAutoreleasePool()
or an NS::AutoreleasePool of its own -- or carries a `// no-pool: <why>` comment on the line above
its definition. A file whose every method runs inside a pool something else pushed says so once,
with a `// no-pool-file: <why>` line.

The rule is textual on purpose: it is what a reader can check, and an exemption is a sentence in
the source rather than an entry in a list somewhere else. What it cannot see -- a helper that
autoreleases, reached from outside every pool -- is what the runtime check in
scripts/tests/test_autorelease_scope.py catches.
"""

import os
import re
from dataclasses import dataclass

METAL_ROOT = os.path.join("libs", "bgpu", "src", "metal")

_DEFINITION = re.compile(r"^\t((?:\w+::)+~?\w+)\(")
_POOLED = ("ScopeAutoreleasePool()", "AutoreleasePool::alloc")
_NO_POOL = re.compile(r"^\s*//\s*no-pool:\s*\S")
_NO_POOL_FILE = re.compile(r"^\s*//\s*no-pool-file:\s*\S", re.M)


@dataclass(frozen=True)
class Unpooled:
    path: str
    line: int
    name: str

    def __str__(self) -> str:
        return (f"{self.path}:{self.line}: {self.name} holds no autorelease pool; open it with "
                f"`const auto pool = ScopeAutoreleasePool();`, or say why it needs none with a "
                f"`// no-pool: <why>` line above the definition")


def _body(lines, start):
    """The lines of the function body opening at or after `start`, and the index past it."""
    i = start
    while i < len(lines) and lines[i] != "\t{":
        i += 1
    depth = 0
    body = []
    for j in range(i, len(lines)):
        depth += lines[j].count("{") - lines[j].count("}")
        body.append(lines[j])
        if depth == 0 and j > i:
            return body, j + 1
    return body, len(lines)


def unpooled_in_source(path, text):
    """Every member function definition in one source that neither pools nor is exempted."""
    if _NO_POOL_FILE.search(text):
        return []
    lines = text.split("\n")
    found = []
    i = 0
    while i < len(lines):
        m = _DEFINITION.match(lines[i])
        if not m:
            i += 1
            continue
        # The comment sits above the return type when it has a line of its own.
        head = i - 1 if i > 0 and lines[i - 1].startswith("\t") and not lines[i - 1].strip().startswith("//") \
            and lines[i - 1].strip() not in ("", "}") else i
        above = lines[head - 1] if head > 0 else ""
        body, after = _body(lines, i)
        text_body = "\n".join(body)
        if not any(p in text_body for p in _POOLED) and not _NO_POOL.match(above):
            found.append(Unpooled(path, i + 1, m.group(1)))
        i = after
    return found


def unpooled(repo_root):
    """Every Metal entry point in bgpu that neither pools nor is exempted, in path order."""
    root = os.path.join(repo_root, METAL_ROOT)
    found = []
    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith(".cpp"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="utf-8") as fh:
                found += unpooled_in_source(os.path.relpath(path, repo_root), fh.read())
    return sorted(found, key=lambda u: (u.path, u.line))
