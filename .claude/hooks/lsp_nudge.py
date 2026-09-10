#!/usr/bin/env python3
"""Point a C++ symbol search at the language server instead of at grep.

A PreToolUse hook on Bash/PowerShell. Unlike gh_guard it **blocks nothing**: grep
over the tree is right for plenty of things -- prose in docs/, a key in a .json or
.bmaterial, a CMake variable, a .slang identifier (Slang has no server here) -- and
a rename still wants grep's exhaustiveness, because clangd's index lags the working
tree. What it catches is the narrow case where the LSP is simply the better tool
and habit reaches for the regex: a bare identifier searched across the C++ sources.

It answers with `hookSpecificOutput.additionalContext`, which reaches the model
without stopping the call, so the search still runs and the reminder arrives beside
it. See the root CLAUDE.md, "Read the code with clangd, not with grep".
"""

import json
import os
import re
import shlex
import sys

SEPARATORS = re.compile(r"&&|\|\||[;|\n]")

SEARCH_TOOLS = ("grep", "egrep", "fgrep", "rg", "ag", "ack")

# A C++ identifier and nothing else: no regex metacharacters, no spaces. A pattern
# with any syntax in it is asking something the LSP cannot answer anyway.
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Worth at least two segments, so a search for `i` or `Get` is not lectured at.
MIN_LENGTH = 4

# Where C++ symbols live. A path outside these is prose, a shader or build files.
CPP_ROOTS = ("libs", "apps", "examples")

CPP_SUFFIXES = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".inl")

# Searched from the tree root rather than piped from another command's output. A bare
# `grep foo` with neither is a filter over stdin and has nothing to do with the sources.
RECURSIVE_FLAGS = {"-r", "-R", "--recursive", "-rn", "-rln", "-rin", "-nr", "-ln"}

ADVICE = (
    "The LSP answers this better than a regex: it knows a declaration from a definition,\n"
    "follows a typealias, skips a comment that happens to say the name, and never answers\n"
    "with another class's identically-named method.\n"
    "\n"
    "    findReferences   who calls it / what breaks if it changes\n"
    "    goToDefinition   where it is defined\n"
    "    hover            what it is and what it returns\n"
    "    workspaceSymbol  where '{symbol}' is, when the file is not known yet\n"
    "\n"
    "The search was allowed and has run. Two cases where grep is still the right answer:\n"
    "a sweep whose completeness decides a change is safe -- a rename, a removed enumerator,\n"
    "the last call site -- because clangd's index lags the working tree; and anything that\n"
    "is not a C++ symbol."
)


def segments(command):
    """The command split into separately-executed pieces, each as tokens."""
    for raw in SEPARATORS.split(command):
        raw = raw.strip()
        if not raw:
            continue
        try:
            tokens = shlex.split(raw, posix=False)
        except ValueError:
            tokens = raw.split()
        yield [t.strip("\"'") for t in tokens]


def searches_cpp(paths, recursive):
    """Whether the paths a search was given are C++ sources."""
    if not paths:
        # No path at all: a tree-wide search reaches the C++ sources, a piped one is
        # filtering somebody else's output and never touched them.
        return recursive

    for path in paths:
        normalized = path.replace("\\", "/").lstrip("./")
        if normalized.endswith(CPP_SUFFIXES):
            return True

        # A named file of some other kind -- .slang, .md, .json, .txt -- is not the LSP's
        # to answer, even under libs/. Only an extensionless path is taken for a directory.
        _, _, tail = normalized.rpartition("/")
        if "." in tail:
            continue

        if normalized.split("/", 1)[0] in CPP_ROOTS:
            return True
    return False


def symbol_of(args):
    """The bare C++ identifier a search is for, or None if it is not one."""
    # Anything that takes a value, so its argument is never mistaken for the pattern.
    takes_value = {"-e", "--regexp", "-f", "--file", "-m", "--max-count", "-A", "-B", "-C",
                   "--include", "--exclude", "--glob", "-g", "-t", "--type", "--color"}

    operands = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            # An -e pattern is still the pattern.
            operands.append(arg)
            continue
        if arg in takes_value:
            skip = True
            continue
        operands.append(arg)

    positional = [o for o in operands if not o.startswith("-")]
    if not positional:
        return None

    pattern = positional[0]
    if not IDENTIFIER.match(pattern) or len(pattern) < MIN_LENGTH:
        return None

    recursive = any(a in RECURSIVE_FLAGS for a in args)
    if not searches_cpp(positional[1:], recursive):
        return None
    return pattern


def tool_args(tokens, name):
    """The arguments following the first invocation of `name` in `tokens`, or None."""
    for i, token in enumerate(tokens):
        base = os.path.basename(token.replace("\\", "/")).lower()
        if base in (name, f"{name}.exe"):
            return tokens[i + 1:]
    return None


def verdict(command):
    for tokens in segments(command):
        for name in SEARCH_TOOLS:
            args = tool_args(tokens, name)
            if args is None:
                continue
            symbol = symbol_of(args)
            if symbol:
                return symbol
    return None


def main():
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return 0

    command = (payload.get("tool_input") or {}).get("command") or ""
    symbol = verdict(command)
    if not symbol:
        return 0

    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "additionalContext": ADVICE.format(symbol=symbol),
        }
    }))
    return 0


if __name__ == "__main__":
    sys.exit(main())
