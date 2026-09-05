"""Which bracket an #include wears, decided by where the header it names actually is.

STYLE.md gives the rule: a header from a subsystem's `include/` tree is that subsystem's
interface and is spelled `<>`; a header from its `src/` is internal and is spelled `""`.
Third-party and standard headers are `<>` like any other interface.

clang-tidy's `misc-include-cleaner` cannot honour it. Every insertion it writes is quoted
except the standard library's -- `"bgl/IScene.h"` beside `"Metal/MTLBuffer.hpp"` -- and there
is no option to change that. So `just tidy --fix` runs this afterwards, over the files it
touched, and the bracket becomes a fact about the header rather than an artefact of which
tool wrote the line.

The decision is the resolved path, never the spelling: the same `"util.h"` is core's public
header from one directory and bgl_extended's internal one from another. Resolution uses the
translation unit's own search path out of `compile_commands.json`, in the order the compiler
would, so a spelling this preset cannot resolve -- a Windows-only header on macOS -- is left
exactly as it was rather than guessed at.
"""

import os
import re

# A quoted path or a bare run of non-space, so a directory under "Program Files" survives.
_ARG = r'"[^"]*"|\S+'

_SEARCH_FLAGS = [
    re.compile(r'(?:^|\s)-I\s*(' + _ARG + r')'),
    re.compile(r'(?:^|\s)-isystem[\s=]+(' + _ARG + r')'),
    re.compile(r'(?:^|\s)-iquote[\s=]+(' + _ARG + r')'),
]

INCLUDE = re.compile(r'^(\s*#\s*include\s+)(["<])([^">]+)([">])(.*)$')


def search_dirs(command, directory):
    """The `-I`, `-isystem` and `-iquote` directories of one compile command, absolute."""
    dirs = []
    for pattern in _SEARCH_FLAGS:
        for match in pattern.finditer(command):
            path = match.group(1).strip('"')
            dirs.append(os.path.normpath(os.path.join(directory, path)))
    return dirs


def resolve(spelling, quoted, own_dir, dirs):
    """Where `spelling` lands, or None when this preset cannot reach it.

    A quoted include searches the including file's own directory first, which is how the
    compiler reads it and the reason `"Scene.h"` beside `Scene.cpp` resolves at all.
    """
    candidates = [own_dir] + dirs if quoted else dirs
    for directory in candidates:
        candidate = os.path.join(directory, spelling)
        if os.path.isfile(candidate):
            return os.path.normpath(candidate)
    return None


def wanted_quoted(target, repo_root):
    """Whether `target` must be spelled `""`, `<>`, or is not ours to say.

    CLAUDE.md draws the line at the directory: a subsystem's `include/` is what it publishes
    and is `<>`, its `src/` is internal and is `""`. Anything that is not our source at all --
    vcpkg, the standard library, and the IDL headers generated under `build/` that this tree
    already spells `<bgl_common/idl/PsoType.h>` -- is an interface by construction.

    None means the convention does not reach this header, and the spelling is then left
    exactly as written. `examples/util` is the case that forces it: its headers sit beside
    the sources that use them, in neither an `include/` nor a `src/`, and the tree spells
    them `<DemoWindow.h>` from another example. Guessing either way there would rewrite
    working code to satisfy a rule nobody wrote.
    """
    try:
        parts = os.path.relpath(os.path.abspath(target), repo_root).split(os.sep)
    except ValueError:  # a different drive on Windows -- certainly not ours
        return False
    if parts[0] in (os.pardir, "build"):
        return False
    directories = parts[:-1]
    if "include" in directories:
        return False
    if "src" in directories:
        return True
    return None


def restyle(text, source, dirs, repo_root):
    """`text` with every resolvable #include wearing the bracket its header earns.

    `source` is the path of the file `text` came from -- both halves of the rule need it:
    the directory a quoted include searches first, and the module that decides whose
    internals the header is.

    Returns (text, changes), where changes lists the (before, after) spellings, so a caller
    can report what it did without diffing.
    """
    changes = []
    own_dir = os.path.dirname(os.path.abspath(source))
    lines = text.splitlines(keepends=True)
    for index, line in enumerate(lines):
        match = INCLUDE.match(line)
        if not match:
            continue
        lead, opener, spelling, closer, trail = match.groups()
        if (opener, closer) not in (('"', '"'), ("<", ">")):
            continue
        quoted = opener == '"'
        target = resolve(spelling, quoted, own_dir, dirs)
        if target is None:
            continue
        wants = wanted_quoted(target, repo_root)
        if wants is None or wants == quoted:
            continue
        brackets = ('"', '"') if wants else ("<", ">")
        # `trail` still carries the CR of a CRLF line, so only the LF has to come back.
        ending = "\n" if line.endswith("\n") else ""
        lines[index] = f"{lead}{brackets[0]}{spelling}{brackets[1]}{trail}{ending}"
        changes.append((f"{opener}{spelling}{closer}", f"{brackets[0]}{spelling}{brackets[1]}"))
    return "".join(lines), changes


def restyle_file(path, entry, repo_root):
    """Rewrite `path` in place against the compile command in `entry`. Returns the changes."""
    dirs = search_dirs(entry.get("command", ""), entry.get("directory", repo_root))
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    fixed, changes = restyle(text, path, dirs, repo_root)
    if changes:
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(fixed)
    return changes
